# Design

## Context

The change plugs into existing seams rather than adding new routing:

- `TaskHandler` / `TaskHandlerFactory` provide the extension point; the bundled nodeagent links handlers through `bundled_extensions`.
- `ChildTaskSubmitter` is already the node-global child submission service. The `default` local scheduler admits a child locally or forwards it upstream through `GatewayClient`.
- `gateway::ResultReceiver` already models an asynchronous child outcome, and the OpenWorkflow `ChildResultReceiver` is the established bridge pattern.
- `piped_executable` is the lifecycle template for `posix_spawn`, `pipe2`, pidfd polling, stderr logging, and deferred deletion.
- Handlers are shared across connections and run on the single event-loop thread. `Task.body` is opaque bytes, and `capacity.concurrency` is already advertised through `ParseConfig`.

Constraints: Python 3 is available in the environment but cloudpickle is not; `rules_python` is declared but the repo has no Python package, pip integration, or Python tests yet. The nodeagent binary supports `data` dependencies, and the e2e harness resolves data dependencies through runfiles.

Behavioral requirements are in `specs/pyfunction-task-handler/spec.md` and `specs/pyfunction-sdk/spec.md`.

## Goals / Non-Goals

**Goals:**

- A `pyfunction` handler with a fixed slot model, persistent Python workers, and both invocation modes.
- Nested calls that reuse `ChildTaskSubmitter`, admission, and the upstream forward path.
- A single Python package that works as both the worker runtime and the client SDK.
- A worker boundary that an embedded C++/libpython worker could implement later without changing the handler or the task contract.

**Non-Goals:**

- An object store, non-blocking object refs, or dependency-aware scheduling (Stage 2/3).
- Cancellation of already-running Python beyond killing the worker.
- Automatic retries, multi-return, or streaming results.
- Non-Python SDKs or changes to the gateway wire protocol.
- Replacing the Python worker with an embedded interpreter.
- Any OpenWorkflow projection change beyond the existing `parameters` map.

## Decisions

### D1: One worker process per slot, admission is the queue

`capacity.concurrency` is both the worker count and the in-flight task limit. Every admitted task owns one slot from `HandleTask` to its final result; the handler has no queue. Overflow is handled by the existing layers: the gateway's capability-aware scheduler sends work to another node, the node's admission controller rejects excess, and the local scheduler forwards unadmitted children upstream.

Alternatives considered:

- **Thread-per-task inside a worker**: better single-node nesting, but introduces GIL contention and concurrent user code for a model the requirements do not need.
- **Queue in the handler**: absorbs bursts, but a queue behind a pool where every worker is blocked on a child is a deadlock. Rejecting or forwarding is explicit and safe.

Consequence: on a single node, nested calls need `concurrency` headroom. With every slot occupied by blocked parents, the child is not admitted and is forwarded; if no node can take it, the pending call fails with `CAPACITY_REFUSED`. This is recorded as a deployment trade-off, not a silent hang.

### D2: Persistent worker process with dedicated protocol pipes

Each slot owns one Python process, spawned with `posix_spawn` on the bootstrap path. The protocol uses a dedicated pipe pair, not stdin/stdout, so user `print()` cannot corrupt frames. Worker stdout and stderr go to the node log stream; the handler reads stderr through io_uring and never forwards it as result data. Exit is detected with a pidfd and reaped with `waitpid(WNOHANG)` after the poll fires.

The worker boundary stays language-agnostic: framed messages over pipes, one task at a time, opaque payloads. An embedded C++ worker can replace the Python process later.

### D3: Length-prefixed protobuf protocol

Each frame is a 4-byte length prefix followed by a serialized protobuf message. The handshake carries a protocol version; a mismatched version or an oversized frame terminates the worker. The message set is:

```
handler -> worker: Hello(version) | Run(task_id, mode, function, parameters, body)
                   | RemoteResult(call_id, body, is_final, status, reason)
                   | Cancel(task_id) | Shutdown
worker -> handler: Ready(pid, version) | RunResult(task_id, body, is_final, status, reason)
                   | RemoteCall(call_id, invocation, requirements, parameters)
```

The handler never parses the invocation payload. `Run` carries the mode (`json` or `pickle`) and the optional function reference; the worker owns all Python semantics. `RunResult` supports `is_final` chunks for a future streaming extension, but v1 sends one final chunk.

Alternatives considered: reusing the TLV wire format (its type ids are global and its 4 KiB read-buffer machinery is tuned for connections) and newline-delimited JSON (base64 overhead, no schema). A dedicated framed protobuf stream is smaller and keeps the internal protocol separate from the gateway wire.

There is one in-flight write per worker; additional frames queue. Writes to one fd are not ordered across separate io_uring submissions, so serializing them is required.

### D4: Two invocation modes, selected by `parameters.function`

The presence of `Task.parameters["function"]` selects reference/JSON mode; its absence selects pickle mode. This keeps the Python SDK on the rich cloudpickle path while letting a non-Python JSON producer use the same handler.

- **Reference/JSON mode**: `embedded:<entry>` reads `python_code` from the body, executes it in a fresh namespace, and resolves `<entry>`; `module:<module>:<qualname>` imports the callable. Arguments come from `args`/`kwargs`, then `input`, then the whole body. The return value is JSON.
- **Pickle mode**: the body is a versioned cloudpickle `Invocation`; the result is a cloudpickle value. The Python SDK and `@strij.remote` use this mode.

Alternatives considered: a single JSON envelope for everything (one parser, but base64 for Python values and an unnatural body for JSON producers) and pickle-only (forces a second receiver contract later). The split keeps each producer natural, at the cost of two body shapes.

The JSON mode requires no interpreter change from a JSON-producing caller: it populates the existing `Task.parameters` map with `function` and passes the JSON body.

### D5: Remote calls reuse the child-task path

A worker's `.remote()` becomes a `RemoteCall` frame. The handler generates the child task id, builds a `Task{type="pyfunction"}`, and calls `ChildTaskSubmitter::Submit` with a receiver that sends a `RemoteResult` frame back to the originating worker. The `default` scheduler then decides local execution or upstream forwarding exactly as it does for any child.

```
worker A          handler            submitter/router         local or remote node
   |                 |                       |                        |
   |--RemoteCall---->|                       |                        |
   |                 |--Submit(child)------->|                        |
   |                 |                       |--admit locally-------->|
   |                 |                       |   or Forward upstream  |
   |                 |<--child outcome-------|                        |
   |<--RemoteResult--|                       |                        |
   | .get() resolves |                       |                        |
```

Alternatives considered: direct worker-to-worker transport (no cross-node story without a new protocol) and a private handler-side scheduler (duplicates admission and forwarding). Reuse of the existing path is the reason nested calls work across nodes with no new routing.

### D6: Slot lifecycle, timeouts, and failure

```
[starting] --Ready--> [idle] --assign--> [running] --final--> [idle]
      |                   ^                  |
      | startup timeout   |                  | crash/disconnect
      v                   |                  v
   [dead] --respawn-------+                [dead] --respawn--> [starting]
```

- Workers are spawned eagerly per slot at handler creation. A task assigned to a starting worker holds the slot until `Ready`.
- `startup_timeout` (proposed 30 s) retires a worker that never becomes ready.
- `task_timeout` (proposed 0, disabled) kills a worker and delivers `DEADLINE_EXCEEDED`; Python cannot be interrupted safely, so killing is the only hard stop.
- `remote_call_timeout` (proposed 300 s) resolves a pending worker call with an error, so a lost child cannot pin a slot forever.
- A crash fails the in-flight task with `INTERNAL`, drops outstanding remote-call receivers, and respawns. The task is not retried automatically because side effects are not idempotent.
- A client disconnect kills the running worker, drops its result, and frees the slot, mirroring `piped_executable`.

Every `HandleTask` must produce exactly one terminal result; otherwise the `AdmissionTrackingSender`'s scope leaks.

### D7: One Python package, two runtimes

The `strij` package exposes `init`, `remote`, `RemoteRef`, and `RemoteError`. A process-global runtime is chosen by context:

- The worker bootstrap installs a pipe-backed runtime before user code runs.
- `strij.init(gateway_url)` installs an HTTP-backed runtime for client processes.
- With neither, `.remote()` raises a clear "no runtime configured" error.

The decorator is identical in both contexts. `fn.remote(...)` returns a `RemoteRef`; `.get()` awaits it. The worker runtime serializes frame writes so calls from multiple threads inside a task are safe.

Eager ref resolution happens while building the invocation: the runtime walks positional and keyword arguments, resolves `RemoteRef` values recursively, including inside lists/tuples/dicts, and then serializes. This is Stage 1: the callee receives values, matching Ray's default argument behavior. A foreign or unreachable ref raises before submission. Returning a ref from a task fails with a clear error. The object-store follow-up (Stage 2/3) can introduce a real object id later; v1 refs remain local and are never serialized.

The invocation envelope carries an explicit version so a future ref-aware encoding is distinguishable.

### D8: Packaging and deployment

The `strij` sources live under `python/strij/` and are shared by both consumers:

- **Worker**: Bazel `py_library` + `py_binary`, with cloudpickle vendored under `third_party/python/cloudpickle` (consistent with the existing vendored `SPSCQueue.h`). The nodeagent binary carries the worker as a `data` dependency and resolves it through runfiles. The handler also accepts an installed `python_executable` + `worker_module` for operators who do not deploy runfiles.
- **Client**: a `pyproject.toml` package declaring `cloudpickle` as a normal PyPI dependency. Users install it; the worker's vendored copy is an implementation detail.
- **CI/tests**: Bazel Python tests run hermetically against the vendored copy; the real-Python e2e suite skips with an explanatory message when Python or cloudpickle is unavailable, following the existing capability-probe pattern.

Alternatives considered: a pip-installed worker everywhere (simpler on paper, but `bazel test` would depend on host state) and a fully hermetic rules_python toolchain plus pip/wheel repositories (most reproducible, but the largest build change and a consumer-module cost). Option B keeps `bazel test` meaningful while giving users a normal package.

### D9: Error mapping

| Condition | `TaskStatus` |
|---|---|
| Malformed body or unsupported envelope version | `MALFORMED_REQUEST` |
| Unresolvable module or entrypoint | `NOT_FOUND` |
| Python exception in the task | `INTERNAL` |
| No assignable worker or worker startup failure | `UNAVAILABLE` |
| Task or startup timeout | `DEADLINE_EXCEEDED` |
| Admission or no eligible node for a child | `CAPACITY_REFUSED` |
| Python worker protocol/version failure | `INTERNAL` |

The client maps the inverse HTTP statuses back to these codes for `RemoteError`.

### D10: Configuration

`PyFunctionTaskHandlerConfig` carries `capacity`, `python_executable`, one of `worker_module` or `worker_executable`, `python_paths`, `startup_timeout`, `task_timeout`, `remote_call_timeout`, and `max_frame_bytes`. Validation rejects zero concurrency, a missing worker target, negative durations, and a zero frame bound at node startup.

## Risks / Trade-offs

- **Single-node nesting needs headroom** -> Document it, prefer `capability_aware` routing, and fail the nested call fast with `CAPACITY_REFUSED` rather than deadlocking.
- **`round_robin` can route to a node without the handler** -> Recommend `capability_aware` for `pyfunction`; keep the remote-call timeout so a dropped submission does not pin the parent.
- **Worker crash loses the task** -> At-most-once semantics; no automatic retry; `INTERNAL` surfaces the failure.
- **A hung task occupies a slot forever** -> Offer `task_timeout`; zero keeps the Ray-like behavior, and operators can set a bound.
- **Eager ref resolution blocks the caller** -> Same slot/exhaustion rules as `.get()`; Stage 2/3 can add non-blocking object refs.
- **Pickle trust and version skew** -> Treat submission as trusted cluster input, version the envelope, and keep client and worker cloudpickle copies compatible.
- **Large inline payloads** -> Enforce `max_frame_bytes`; a future object store moves large values out of the task body.
- **Vendored cloudpickle diverges from the client's PyPI copy** -> Pin one version range in both places and test round-trips across both copies.
- **Worker user output corrupts the protocol** -> Dedicated protocol fds; redirect `sys.stdout` to the log stream in the bootstrap.
- **Runfiles deployment** -> Provide the installed `python_executable` + `worker_module` configuration path for production.
- **Python worker and sanitizer jobs** -> C++ tests use a fake worker; real-Python e2e skips when unavailable; ASan/TSan never instrument the Python interpreter.

## Migration Plan

The capability is additive. Existing nodeagent configurations remain valid; a node opts in by adding a `pyfunction` entry to `task_handlers` and ensuring a Python runtime plus the worker package are available. Removing the entry, or rolling back the extension, makes `pyfunction` submissions fail through the existing no-handler path; other task types are unaffected. No gateway, scheduler, or wire-protocol migration is required.

## Open Questions

None. The slot model, invocation modes, ref-resolution stage, worker implementation, and packaging boundary are settled; remaining choices (exact default timeouts and the vendored cloudpickle pin) are configuration and implementation details, not architectural unknowns.
