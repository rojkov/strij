## 1. Task proto package

- [x] 1.1 Create `api/core/task/task.proto` with `Task { uint64 id = 1; string type = 2; bytes body = 3; }` and `TaskResult { uint64 id = 1; bytes body = 2; }` in package `carrot.task`
- [x] 1.2 Create `api/core/task/BUILD.bazel` with `proto_library` and `cc_proto_library` targets (mirroring `api/core/config/BUILD.bazel`)

## 2. LlhttpParser request awareness

- [x] 2.1 Add `struct HttpRequest { std::string_view path; std::span<const std::byte> body; }` to `src/core/io/llhttp_parser.hh`
- [x] 2.2 Register the llhttp `on_url` callback and accumulate URL fragments into a `std::string` member in `LlhttpParser`
- [x] 2.3 Change the `LlhttpParser` callback signature from `std::move_only_function<void(std::span<const std::byte>)>` to `std::move_only_function<void(HttpRequest)>` and update `finalizeMessage()` to deliver path + body
- [x] 2.4 Add `test/core/io/llhttp_parser_test.cc` covering path capture, body delivery, and partial-request handling

## 3. Gateway task creation

- [x] 3.1 Factor task-type extraction into a testable helper (e.g. `ParseTaskType(std::string_view path)`) that strips the `/tasks/` prefix and query string and returns empty for non-matching paths
- [x] 3.2 Update `GatewayHttpHandler::HandleMessage` to accept `HttpRequest`, respond 404 for paths not starting with `/tasks/`, and 400 for empty type
- [x] 3.3 Build and serialize a `Task` proto (id = monotonic counter, type = extracted value, body = request body) and wrap it in a `kTaskSubmission` TLV frame via `SerializeTlvFrame()`
- [x] 3.4 Update the gateway connection factory in `src/exe/gateway/gateway.cc` to pass `HttpRequest` to the handler

## 4. Nodeagent task handling

- [x] 4.1 Update `NodeagentTlvHandler::HandleFrame` to parse the value as a `Task` proto, echo a `TaskResult` (id + body), and log-and-drop malformed frames
- [x] 4.2 Add `//api/core/task:task_cc_proto` dependency to `//src/core/nodeagent:nodeagent_tlv_handler_lib`

## 5. Gateway result handling

- [x] 5.1 Update `GatewayTlvHandler::HandleFrame` for `kResult` frames to parse a `TaskResult` proto, look up the receiver by `id`, deliver `body`, and log-and-drop malformed frames
- [x] 5.2 Add `//api/core/task:task_cc_proto` dependency to `//src/core/gateway:gateway_tlv_handler_lib` and `//src/core/gateway:gateway_http_handler_lib`

## 6. Build wiring

- [x] 6.1 Add `//api/core/task:task_cc_proto` dependency to the gateway and nodeagent binary targets in `src/exe/gateway/BUILD.bazel` and `src/exe/nodeagent/BUILD.bazel`

## 7. Tests

- [x] 7.1 Add unit tests for the task-type extraction helper (prefix match, query stripping, empty type, non-`/tasks/` path)
- [x] 7.2 Rewrite the wire-format assertions in `test/core/gateway/gateway_test.cc` to serialize/parse `Task` and `TaskResult` protos instead of `[task_id:8][payload:N]`
- [x] 7.3 Add a `GatewayTlvHandler` test parsing a `TaskResult` frame and delivering the body to the receiver, including a malformed-frame drop test
- [x] 7.4 Add a `NodeagentTlvHandler` test parsing a `Task` frame and emitting a `TaskResult`, including a malformed-frame drop test
- [x] 7.5 Run `make test` and confirm all tests pass
