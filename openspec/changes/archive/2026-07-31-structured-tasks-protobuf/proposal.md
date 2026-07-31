## Why

Tasks between gateway and nodeagent are currently hand-rolled binary blobs (`[task_id:8][http_body:N]`), so the nodeagent cannot interpret or route them. To prepare for multiple pluggable task handlers, tasks need a structured, self-describing wire format carrying an `id`, a `type`, and a `body` — Protobuf provides this serialization.

## What Changes

- Add Protobuf messages `Task { id, type, body }` and `TaskResult { id, body }` in a new `api/core/task/` package.
- **BREAKING**: TLV value for `kTaskSubmission` changes from `[task_id:8][body:N]` to a serialized `Task` message. TLV value for `kResult` changes from `[task_id:8][body:N]` to a serialized `TaskResult` message.
- `LlhttpParser` becomes request-aware: it captures the URL path (via llhttp `on_url`) and delivers an `HttpRequest { path, body }` struct to its callback instead of a raw body span.
- `GatewayHttpHandler` extracts the task `type` from the URL path (`POST /tasks/{type}`): strips a query string, responds 404 for non-`/tasks/` paths and 400 for empty type, builds and serializes a `Task`, and submits it over TLV as before.
- `NodeagentTlvHandler` parses the incoming `Task`, echoes back a `TaskResult` (the existing hardcoded echo behavior, operating on the new format). Malformed protobuf frames are logged and dropped.
- `GatewayTlvHandler` parses `TaskResult` to recover `id` and result body for receiver lookup.
- Tests updated: existing wire-format assertions in `gateway_test.cc` change to proto round-trips; new coverage for URL extraction, type parsing, and proto serialization.

## Capabilities

### New Capabilities
- `task-protocol`: Defines the `Task` and `TaskResult` Protobuf message schemas exchanged over TLV between gateway and nodeagent.

### Modified Capabilities
- `gateway-task-bridge`: Task creation, result handling, and nodeagent echo change from raw `[task_id][body]` values to `Task`/`TaskResult` protobuf messages.
- `typed-tlv-messages`: The TLV wire format's type-specific value content changes — task frames now carry serialized protobuf instead of `[task_id:8][payload:N]`.
- `protocol-parser`: `LlhttpParser` now captures and delivers the request path alongside the body via an `HttpRequest` struct.

## Impact

- **New files**: `api/core/task/task.proto`, `api/core/task/BUILD.bazel`.
- **Modified**: `src/core/io/llhttp_parser.{hh,cc}`, `src/core/gateway/gateway_http_handler.{hh,cc}`, `src/core/gateway/gateway_tlv_handler.cc`, `src/core/nodeagent/nodeagent_tlv_handler.cc`, corresponding `BUILD.bazel` files (new protobuf dependency), `test/core/gateway/gateway_test.cc`.
- **Build**: adds `@protobuf` protoc-generated code to the io/gateway/nodeagent targets.
- **Out of scope** (separate proposals): nodeagent task-handler dispatch/extensions; gateway validation of unknown task types; `GET /tasks` listing endpoint.
