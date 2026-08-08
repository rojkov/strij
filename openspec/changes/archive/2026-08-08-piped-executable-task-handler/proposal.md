## Why

Strij is a distributed runtime for serverless functions, but today the only task handler is `echo` — a stub that reproduces its input. To run actual functions we need a handler that executes a user-provided executable on a node: feed the task body via stdin, stream stdout back to the HTTP client, and finish when the process exits.

## What Changes

- **`Task` gains a generic `parameters` map** (`map<string,string> parameters = 4`) for per-request metadata.
- **HTTP header parameters**: request headers named `x-strij-<name>` are forwarded to the node as `parameters[<name>]` (the prefix is stripped and lowercased). `x-strij-function: /path/to/bin` → `parameters["function"]`. Headers are now captured by `LlhttpParser` into an extended `HttpRequest`.
- **New task handler extension `piped_executable`**: on task arrival, spawn the executable named by `parameters["function"]` via `posix_spawn`, feed `task.body` to its stdin, stream its stdout back as non-final `TaskResult` chunks, log its stderr at the node, and deliver the final result when the process exits.
- **Streaming result delivery over HTTP**: if the first result chunk is not final, the gateway keeps the result receiver until the final chunk and streams the response using `Transfer-Encoding: chunked`; single-shot results keep the existing `Content-Length` behavior.
- **`Dispatcher` gains a `PreparePoll` op** (io_uring `poll_add`) used to detect process exit via `pidfd_open`.
- **`FunctionResolver` seam**: a shared, injectable resolver (`LocalFunctionResolver` today; a cache-backed resolver after the deployment extension lands) exposed via `FactoryContext`, so every future handler that consumes a `function` parameter shares one resolution policy.
- **AGENTS.md** updated to record `posix_spawn` / `pidfd_open` / reaping `waitpid` as accepted non-`io_uring` synchronous calls.
- No breaking protocol changes: `Task.parameters` is additive; existing handlers and single-shot results are unaffected.

## Capabilities

### New Capabilities

- `task-parameters`: parameters flow end-to-end — `LlhttpParser` header capture, `HttpRequest.headers`, the `x-strij-<name>` → `Task.parameters` forwarding convention at the gateway, and the shared `function` well-known key.
- `streaming-task-results`: multi-chunk result delivery — finality-aware receiver retention in `GatewayTlvHandler`, the `ResultReceiver::Deliver(..., is_final)` contract, and the `Content-Length` vs `Transfer-Encoding: chunked` framing decision in `HttpResultReceiver`.
- `function-resolver`: the `FunctionResolver` interface, the `LocalFunctionResolver` implementation, and its injection through `FactoryContext` so all function-consuming handlers share one resolution policy.
- `piped-executable-task-handler`: the extension itself — `posix_spawn` process spawning, stdin/stdout/stderr pipe plumbing through the io_uring dispatcher, `pidfd`-based exit detection, streaming stdout delivery, local stderr logging, and `LocalFunctionResolver` integration.

### Modified Capabilities

- `task-protocol`: `Task` message schema gains `map<string,string> parameters = 4`.
- `gateway-task-bridge`: `GatewayHttpHandler` forwards `x-strij-*` headers into task parameters; `GatewayTlvHandler` keeps the result receiver until the final chunk.
- `protocol-parser`: `HttpRequest` gains headers; `LlhttpParser` captures them via llhttp header callbacks.
- `dispatcher-connect`: `Dispatcher` gains the `PreparePoll` operation (io_uring `poll_add`).

## Impact

- **Core:** `api/core/task/task.proto`, `src/core/io/llhttp_parser.{hh,cc}`, `src/core/gateway/gateway_http_handler.{hh,cc}`, `src/core/gateway/gateway_tlv_handler.cc`, `src/core/gateway/http_result_receiver.{hh,cc}`, `src/core/gateway/result_receiver_storage.hh`, `src/core/extensions/factory_context.{hh,cc}`, `include/strij/event/dispatcher.hh`, `src/core/event/dispatcher_impl.{hh,cc}`, `test/mocks/event/mocks.{hh,cc}`, `src/exe/nodeagent/nodeagent.cc`.
- **New:** `api/extensions/task_handlers/piped_executable/piped_executable.proto`, `src/extensions/task_handlers/piped_executable/`, `FunctionResolver` + `LocalFunctionResolver` library, matching tests.
- **Docs:** `AGENTS.md` synchronous-call carve-out note.
- **Deferred (follow-up changes):** `TaskResult` error/exit-code surfacing (spawn failures currently deliver an empty final result); per-task cancellation + result-receiver lifecycle on connection drop; task timeouts; migrate the pre-existing `CLOSE_CONNECTION` command onto the generic `DEFERRED_DELETE` command and document the deferred-delete pattern. Concurrency limiting is out of scope (future gateway-side resource accounting and routing).
