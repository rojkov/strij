# Design

## Context

See `proposal.md` — Why. The constraints that shape the approach:

- `OpenWorkflowTaskHandler::HandleTask` currently parses `task.body()` as the workflow document (`openworkflow_task_handler.cc:46`), and `WorkflowInterpreter::Start` seeds the data chain from `document.input.from` evaluated against JSON `null` (`interpreter.cc:202-220`). There is no function reference today.
- The gateway already forwards `x-strij-function` into `task.parameters["function"]` (`gateway_http_handler.cc:36-56`). No gateway, protocol, or wire change is needed.
- `FunctionResolver::Resolve(std::string_view) -> absl::StatusOr<std::string>` is frozen for this change. Widening it to an artifact locator, type-aware resolution, and materialization is a follow-up.
- The repository requires `io_uring` for I/O, with narrow synchronous exceptions. This change adds one: a scoped synchronous artifact load in the `openworkflow` handler, revisited when the resolver takes over materialization.
- Node advertisement is type-only: every node that advertises `openworkflow` is assumed able to resolve the referenced artifact. There is no per-function placement yet.
- The existing handler keeps a `runs_` map keyed by task id, registers `RegisterOnClose`, and lets the interpreter own child tasks. That lifecycle is retained.

## Goals / Non-Goals

**Goals:**

- Make the workflow document a function artifact selected by `parameters["function"]`, with the task body as invocation input.
- Load the artifact without expanding the `Dispatcher` surface or introducing per-task async loader state.
- Keep the interpreter's existing resumable lifecycle and exactly-one-final-result behavior intact.
- Give pre-interpretation failures distinct, externally visible statuses.

**Non-Goals:**

- `FunctionResolver` semantic widening, type-awareness, materialization, or the function plane.
- Definition caching, reload semantics, or deployment/routing by function.
- Async file I/O or a new `Dispatcher::PrepareOpenAt`.
- A legacy body-as-document fallback.
- Changing `piped_executable`, the gateway, or the wire protocol.

## Decisions

### D1 — The reference is the existing `function` parameter; the resolver is unchanged

`HandleTask` reads `task.parameters["function"]` through `kFunctionParameter`, then calls the shared `FunctionResolver` obtained from `TaskHandlerDeps` at factory construction. The returned string is treated as a local path by this handler. Resolution errors (including an empty reference) are the client's problem and become `MALFORMED_REQUEST`.

**Alternative rejected:** a new task type for workflow functions. The task type identifies the executor, not the artifact; `openworkflow` already is the executor. A new type would fragment the capability advertisement and the `call: strij` projection.

**Alternative rejected:** having the resolver return document bytes. That is the follow-up's decision, and it would force the resolver to change before the function plane exists.

### D2 — Synchronous, bounded artifact load as a scoped exception

The loader is a free function (`LoadWorkflowArtifact(path) -> absl::StatusOr<std::string>`) so it can be unit-tested independently of the handler. It performs:

```
open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK)
fstat(fd)            -> require S_ISREG
size = st_size       -> reject size < 0 or size > 4 MiB
read(fd, ...)        -> loop on EINTR and short reads until size bytes or EOF
close(fd)            -> on every path
```

`O_NONBLOCK` is essential and is not about performance: without it, `open` on a client-supplied FIFO blocks the event-loop thread *before* any `fstat` can reject it. For regular files the flag is ignored; for FIFOs and devices it makes the open return immediately so the `S_ISREG` check can reject the path. The 4 MiB cap is an implementation constant, not configurable in this change.

Because the load is synchronous and happens inside `HandleTask` on the event-loop thread, a connection close cannot interleave mid-load. No loader state, `Completable`, `DEFERRED_DELETE`, or dispatcher change is needed.

**Alternative rejected:** async chunked reads via the existing `PrepareRead`. While more faithful to the `io_uring` rule, it adds a per-task loader object, a close/cancel window, and a second completion path for a document expected to be small. The exclusion is deliberately narrow and carries a revisit trigger.

**Alternative rejected:** `std::ifstream`. It hides `errno` and complicates distinguishing "missing" from "unreadable", which the status contract needs.

**No cache:** every invocation re-reads the artifact. This matches `piped_executable`'s per-task behavior and avoids an invalidation story. Editing the file affects the next invocation.

### D3 — The body is the invocation input; `input.from` transforms it

`WorkflowInterpreter::Start` gains an invocation-input parameter. The seed is:

```
invocation = body.empty() ? JSON null : StepDataValue::FromBytes(body)

if document.input.from exists:
    subject = invocation.IsJson() ? &invocation.Json() : nullptr
    seed = renderFrom(document.input.from, subject)
    data_ = StepDataValue::FromBytes(seed)
else:
    data_ = invocation
```

Consequences:

- A non-empty JSON body seeds the chain directly.
- A non-empty non-JSON body seeds an opaque chain; expressions then fail with "the data subject is not JSON".
- An empty body behaves exactly like today's no-input default: JSON `null`. With `input.from`, that null is the subject.
- `$context` remains a separate, empty accumulator; it is not the body and not the data chain.
- Step-level `input.from` continues to transform the current data chain into the child body.

**Alternative rejected:** body always wins and `input.from` is ignored when a body is present. That makes the DSL field mode-dependent and contradicts the confirmed decision that `input.from` receives the body as subject.

**Alternative rejected:** an empty body as an opaque empty chain. It would break every existing document that relies on the no-input default, which today is JSON `null`.

### D4 — Pre-interpretation failures map onto the existing status set

The loader returns `absl::Status`; the handler maps it to `task::TaskStatus` and sends one final `TaskResult` with that status. No run entry is installed and no document parsing begins.

| Failure | `TaskStatus` | HTTP |
|---|---|---|
| `parameters["function"]` absent | `MALFORMED_REQUEST` | 400 |
| resolver rejects the reference (inc., empty) | `MALFORMED_REQUEST` | 400 |
| path missing (`ENOENT`/`ENOTDIR`) | `NOT_FOUND` | 404 |
| non-regular file or over 4 MiB | `MALFORMED_REQUEST` | 400 |
| other `open`/`read` failure | `INTERNAL` | 500 |
| document parse / pre-execution validation | `MALFORMED_REQUEST` (existing) | 400 |
| recognized-but-unsupported call target | `UNIMPLEMENTED` (existing) | 501 |

This reuses the classification established by the interpreter; the new part is only the prelude. The prior `piped_executable` behavior of delivering an empty *success* on a missing function is not copied.

### D5 — The synchronous prelude runs before the run is installed

`HandleTask` performs: parameter lookup → resolution → load → interpreter construction → run installation → `RegisterOnClose` → `Start(document, body)`. Because the load is synchronous on the loop thread, nothing can observe a half-loaded run. On a prelude failure the handler builds a final error result directly; on success the existing run/close/reap flow is unchanged.

```
HandleTask                    Loader                 Interpreter
    |                            |                        |
    |-- function param? ---------|                        |
    |-- Resolve(reference) ----->|                        |
    |-- LoadWorkflowArtifact --->|                        |
    |<-- document text ----------|                        |
    |-- install run + on-close -------------------------->|
    |-- Start(document, body) --------------------------->|
    |                                                    |-- parse/validate/seed/run
```

The interpreter owns the body seed; the handler does not inspect the body.

### D6 — Documentation and the I/O exception are part of the change

- `AGENTS.md` records the scoped exception next to the existing `posix_spawn`/`pipe2`/`pidfd_open` allowances, with the revisit trigger: when the function resolver takes over artifact materialization.
- `docs/testing-openworkflow.md`, the handler tests, and the e2e fixture are rewritten to store the document at a path, pass it via `x-strij-function`, and use the body as input.
- The `task-parameters` spec is widened from "executable" to "function artifact"; the `openworkflow-interpreter` purpose is updated to describe loading a document artifact and seeding from the body.
- The `function-resolver` spec still says "executable path". That wording is deliberately left for the follow-up; behavior is unaffected because the handler resolves the reference through the resolver and then reads the resolved locator, just as `piped_executable` spawns the resolved locator.

## Risks / Trade-offs

**[A slow filesystem or network mount blocks the event loop for the duration of the read]** → The loader requires `S_ISREG`, caps the artifact at 4 MiB, and retries `EINTR`; the residual risk is accepted by the explicit exception. Revisit with the resolver/async loading.

**[Node-local artifacts plus type-only scheduling produce intermittent `NOT_FOUND`]** → Deploy artifacts uniformly or mount shared storage. The real fix is per-function placement in the function plane, which is out of scope.

**[The hard cut breaks existing body-as-document clients]** → Document the change and the new invocation shape; there is intentionally no fallback, so the failure is immediate (`MALFORMED_REQUEST` on the missing parameter) rather than silent.

**[A client-supplied path can point at special files]** → `O_NONBLOCK` + `S_ISREG` rejects FIFOs and devices without blocking; no path allowlist is added, matching the trust level of `piped_executable` (which already executes client-named paths). The handler only parses, never executes, the artifact.

**[The 4 MiB cap may reject documents with large inline schemas]** → The error is explicit; adjust the constant if real documents need more. Do not silently truncate.

**[`input.from` now sees a body where it previously saw `null`]** → Only applies when a non-empty body is actually supplied, which is the new invocation model. Empty-body invocations preserve today's behavior exactly.

**[Short reads / `EINTR` on regular files]** → The read loop continues until the expected size or EOF; a file that shrinks mid-read yields a shorter document, which then fails parsing as a malformed request.

## Migration Plan

No config, protocol, or gateway migration. The nodeagent binary and docs change together. Existing workflow documents remain valid as artifacts; only their invocation changes: place the document at a resolvable path and send the input in the body. Rollback is reverting the handler, docs, and the `AGENTS.md` exception; no persisted state is involved.

## Open Questions

- Whether to expose the artifact size cap as configuration. Deferred; a constant is sufficient while the function plane is absent.
- Whether to populate `HandlerCapability.function_sourced` for `openworkflow` (and `piped_executable`). Deferred to the function-plane/resolver follow-up, where the flag has a consumer.
- Whether definitions should be cached or reloaded when the artifact changes. Deferred; per-invocation reads match `piped_executable` and avoid invalidation semantics.
