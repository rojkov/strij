## Context

Strij is a C++23, io_uring-based distributed runtime for serverless functions. A gateway accepts HTTP `POST /tasks/{type}` requests and forwards them as protobuf `Task` messages over TLV frames to nodeagents; nodeagents route each task by `task.type()` to a registered `TaskHandler` extension, which delivers `TaskResult` messages back through a `ResultSender`.

Today the only handler is `echo`. This change adds `piped_executable`: a handler that executes a user-supplied binary, feeding the task body on stdin and streaming stdout back to the HTTP client. It also introduces the parameter-transport and streaming-result infrastructure the whole runtime needs to grow on top of.

Constraints from `AGENTS.md`: no blocking synchronous syscalls on the event loop; io_uring preferred, with a documented fallback carve-out for bootstrap/fallback paths. The nodeagent event loop is single-threaded; a dedicated logger thread runs its own io_uring ring (this matters for process spawning).

## Goals / Non-Goals

**Goals:**
- Execute a binary named by a per-task parameter, feed stdin, stream stdout to the HTTP client, finish on process exit.
- Establish a generic per-task parameter transport (HTTP header → `Task.parameters` → handler) reusable by all future handlers.
- Establish multi-chunk result streaming (multiple `TaskResult`s per task) with correct HTTP framing.
- Leave a seam (`FunctionResolver`) for the future deployment extension that fetches curated executables into a local cache.

**Non-Goals:**
- Error/exit-code surfacing to clients (follow-up: `TaskResult` error field).
- Per-task cancellation and receiver lifecycle on connection drop (follow-up).
- Task timeouts (follow-up).
- Concurrency/resource limiting (future gateway-side resource accounting and routing).
- Executable allowlisting / security hardening (future deployment extension).

## Decisions

### D1. Generic `parameters` map with a well-known `function` key

`Task` gains `map<string,string> parameters = 4`. The executable is passed under the shared key `function` (`x-strij-function` → `parameters["function"]`).

Rationale: a first-class `string function` field was considered, but the deployment extension will give `function` real structure (version, digest, source) — that is when a structured field becomes unavoidable, and it lands in the same change as the resolver swap. A bare string field now would buy a migration with no benefit. The map keeps header transport trivially generic (D2) and serves arbitrary future parameters.

### D2. `x-strij-<name>` header convention

`LlhttpParser` captures all request headers (llhttp `on_header_field` / `on_header_value` callbacks) into an extended `HttpRequest.headers`. `GatewayHttpHandler` forwards only headers matching the `x-strij-` prefix: the prefix is stripped and the key lowercased (HTTP header names are case-insensitive and llhttp preserves case), so `X-STRIJ-Function` ≡ `x-strij-function` ≡ `parameters["function"]`. The gateway knows the namespace rule but nothing about individual parameters — a generic parameter-pipe, not handler knowledge.

### D3. Result streaming: single-shot stays `Content-Length`, streaming becomes chunked

The HTTP framing decision is made once, at the first result frame, deterministically from that frame's finality:

- first chunk final → existing single-shot `HTTP/1.1 200 OK` + `Content-Length: N` + body + `Connection: close` (unchanged);
- first chunk non-final → `Transfer-Encoding: chunked` response, one chunk frame per `TaskResult`, terminal `0\r\n\r\n` on the final result.

`ResultReceiver::Deliver(span)` becomes `Deliver(span, bool is_final)`. `GatewayTlvHandler` computes finality with the proto rule (`!has_is_final() || is_final()`), keeps the receiver on non-final chunks, and erases it only on the final chunk. `HttpResultReceiver` is a 3-state machine: `Idle → (first frame) → Done | Chunked → Done`. The node side already supports multi-shot async delivery (`ResultSender` docs: zero or more `Send()` with a final `is_final=true`), so no TLV/protocol change is needed on the wire.

### D4. `posix_spawn` for process creation (no io_uring involvement)

`fork()` was rejected: the child of a multi-threaded nodeagent inherits a heap snapshot containing locks the (vanished) logger thread may hold, and any non-async-signal-safe call before `execve` risks deadlock. `posix_spawn` is safe (glibc implements it via `clone`) and its `posix_spawn_file_actions_adddup2` provides stdin/stdout/stderr redirection. There is no io_uring spawn op, so this is an accepted synchronous call; `AGENTS.md` is updated to record the family (`posix_spawn`, `pidfd_open`, reaping `waitpid`) alongside the existing fallback examples.

### D5. Exit detection via pidfd + io_uring poll

`Dispatcher` gains `PreparePoll(Completable*, tag, fd, poll_mask)` backed by `io_uring_prep_poll_add`. Each child gets `pidfd_open(pid)`; `PreparePoll(pidfd, POLLIN)` fires exactly when that process exits. On fire: `waitpid(pid, &status, WNOHANG)` reaps (guaranteed not to block — poll already fired), then a final non-blocking drain of the stdout pipe (loop read → EAGAIN), then the final result is delivered.

SIGCHLD + `signalfd` was the alternative considered: it requires a global reap-all sweep per signal, entangles the existing `SignalMonitor` (which currently shuts down on any signal), and is coarse once many handlers spawn children concurrently. pidfd gives per-task granularity with zero signal handling. Kernel floor is 5.3 (pidfd) — already below the io_uring requirement, so no new platform constraint.

### D6. One `Completable` per child

Each in-flight task is its own `ChildProcess` object implementing `event::Completable` — the same pattern as `Connection`. The shared `PipedExecutableTaskHandler` owns `map<task_id, unique_ptr<ChildProcess>>`; the dispatcher routes completions by `Completable*` in the sqe user-data, so per-task tags fall out for free.

```
 ChildProcess (event::Completable)          fds: stdin_w, stdout_r, stderr_r, pidfd
   tags: {kStdinWrite=0, kStdoutRead=1, kStderrRead=2, kExitPoll=3}
```

All completions run on the single event-loop thread; no locking. `HandleTask` transfers ownership of the `ResultSender` to the handler by move; `ChildProcess` holds it for the task lifetime. On spawn failure the child itself delivers an empty final result before the handler drops it. `RegisterOnClose` fires if the gateway link dies → `kill(pid)` + cleanup.

Ownership teardown is deferred via the dispatcher's command mechanism, mirroring `Connection::onEndOfStream()` (`SubmitCommand(CLOSE_CONNECTION, owner, this)`). When a `ChildProcess` finishes it submits a `DEFERRED_DELETE` command to the handler; commands are drained at the top of the next `Run()` iteration, so the handler's `ProcessCommand` erases the map entry (destroying the `ChildProcess`) outside the completion stack. `PipedExecutableTaskHandler` therefore implements `event::CommandHandler`, and `Command::Type` gains a generic `DEFERRED_DELETE` value (for delete commands, `type_` is redundant with `(destination_, args_)` — the owner already knows how to destroy its own child; migrating the pre-existing `CLOSE_CONNECTION` onto `DEFERRED_DELETE` is a follow-up). The `Command.args_` carries the `ChildProcess*` (or task id); the handler erases the map entry owning it.

### D7. stdout EOF ≠ process exit

The child may fork grandchildren that keep the pipe's write end open, so stdout EOF can lag the direct child's exit indefinitely. Finality keys off the pidfd poll (the direct child), not EOF: on poll → reap → non-blocking drain → final result. Output written by grandchildren after the drain is the known v1 edge.

### D8. stderr is logged locally, never forwarded

A third pipe (`stderr_r`, tag `kStderrRead`); each read chunk is emitted through the node's `LOG_*` macros (per-chunk, capped). The child blocks on a full stderr pipe otherwise, so it must be read.

### D9. `FunctionResolver` is shared nodeagent infrastructure

Because `function` is a cross-handler concept in a serverless runtime, resolution policy must not be private to one handler. `FactoryContext` gains `FunctionResolver()`. `nodeagent.cc` builds a `LocalFunctionResolver` (v1: the reference *is* the path) and injects it via `FactoryContextImpl`. The deployment-extension change swaps one line in `nodeagent.cc` for a cache-backed resolver and every function-consuming handler upgrades at once.

### D10. Interim spawn-failure behavior

Until the `TaskResult` error follow-up lands, `posix_spawn` failure (e.g. missing path) is logged and an **empty final result** is delivered so the HTTP connection still closes (no hang). A non-zero exit code likewise delivers whatever was streamed, without surfacing the code.

## Sequence diagrams

### D11. Task submission and spawn

```
HTTP client         GatewayHttpHandler      nodeagent              PipedExecutableTaskHandler
    │ POST /tasks/piped_executable           TLV kTaskSubmission          │
    │ x-strij-function: /usr/bin/cat  ───────────────▶ NodeagentTlvHandler│
    │                                          HandleFrame → manager.GetHandler(type)
    │                                                   │ HandleTask(task, sender)
    │                                                   │  resolve(parameters["function"]) ── LocalFunctionResolver
    │                                                   │  pipe(stdin); pipe(stdout); pipe(stderr)
    │                                                   │  posix_spawn(path, dup2 stdin/stdout/stderr)
    │                                                   │  PrepareWrite(stdin_w, task.body)
    │                                                   │  PrepareRead(stdout_r); PrepareRead(stderr_r)
    │                                                   │  PreparePoll(pidfd, POLLIN)
    │                                                   │  sender.RegisterOnClose(kill-cb)
```

### D12. Result streaming back

```
child            ChildProcess            gateway GatewayTlvHandler     HttpResultReceiver
  stdout ──────▶ kStdoutRead ── Send(result, is_final=false) ──▶ Deliver(body, false)
  stdout ──────▶ kStdoutRead ── Send(result, is_final=false) ──▶ Deliver(body, false) ─▶ chunk frame
  exit ────────▶ kExitPoll ── waitpid + drain ── Send(result, is_final=true) ─▶ Deliver(body, true)
                                                                  └─ storage.erase(id) └─ terminal 0\r\n\r\n
```

### D13. Pure seams for testability

The parameter-mapping and HTTP-framing logic sits behind async I/O that makes end-to-end unit tests verbose and brittle: `Connection::Write` only calls `PrepareWrite` when the write queue transitions empty→1, so with `MockDispatcher` (which never drains the queue or writes to a socket) only the first write is observable, and driving `kWrite` completions manually to observe the rest is fragile. Two pure seams mirror the existing `ParseTaskType` free-function pattern:

- **`PopulateParametersFromHeaders(Task&, headers)`** — free function in `gateway_http_handler.hh` (declared like `ParseTaskType`, defined in `.cc`). `HandleMessage` calls it; tests assert directly on the resulting `task.parameters`.
- **`HttpResponseFramer`** — small header-declared stateful class returning `std::vector<std::vector<std::byte>>` frames (single-shot vs chunked decision, hex chunk sizes, terminal chunk). `HttpResultReceiver::Deliver` becomes `for (frame : framer_.Next(body, is_final)) conn_.Write(frame)`; tests assert on the returned frames with no `Connection`/dispatcher involved.

## Risks / Trade-offs

- [Unbounded child output / slow client] → write queue grows without bound (pre-existing for large single results; streaming amplifies it). Mitigated in a follow-up; acceptable for v1.
- [Child never exits] → HTTP connection pinned open. Deferred to timeout + cancellation follow-ups.
- [Client or node link dies mid-stream] → receiver may leak / HTTP client hangs. Deferred to cancellation/receiver-lifecycle follow-up.
- [Orphans on node SIGKILL] → children reparent to init and keep running. `prctl(PR_SET_PDEATHSIG)` needs a pre-exec hook `posix_spawn` cannot give portably; accept for v1, kill tracked children during graceful shutdown.
- [stdout data from grandchildren after drain] → last-chunk truncation possible. Known v1 edge.
- [`PreparePoll` on pidfd portability] → needs kernel ≥5.3; io_uring already requires similar. Verified against the dispatcher's existing op pattern.
- [Pipe read/write offset semantics] → `PrepareRead`/`PrepareWrite` pass `off_t` straight through; pipes want `-1` (Connection passes `0` for sockets/eventfd today). Implementer confirms offset on first test.
- ["delete this" during completion] → teardown is deferred: `ChildProcess` submits a `DEFERRED_DELETE` command on finish and the handler erases it from the map in `ProcessCommand` (the existing `Connection::onEndOfStream` pattern). `ProcessCommand` runs outside the completion stack, so no `this` is touched after the erase.

## Migration Plan

- Fully additive: `Task.parameters` and `HttpRequest.headers` extend messages/structs; existing handlers and single-shot results are untouched.
- Rollback: revert the gateway header-forwarding and the new extension; single-shot path is byte-identical to today.

## Open Questions

- None blocking. Implementation details to confirm on first test: pipe offset (`-1` vs `0`), pidfd poll flags, and partial-stdin-write looping (bodies larger than `PIPE_BUF` need the same offset-loop `Connection` uses for its write queue).
