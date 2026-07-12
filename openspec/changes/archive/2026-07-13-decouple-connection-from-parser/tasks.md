## 1. New Interfaces

- [x] 1.1 Create `ProtocolParser` abstract class in `src/core/io/protocol_parser.hh` with `enum class Action { NeedMoreData, MessageComplete }`, `GetReadBuffer() -> std::span<std::byte>`, `OnData(size_t) -> Action`, and `OnMessage` callback
- [x] 1.2 Create `MessageHandler` abstract class in `src/core/io/message_handler.hh` with `OnMessage(std::span<const std::byte>, Connection&)` — forward-declare `Connection`

## 2. Refactor LlhttpParser

- [x] 2.1 Remove `IOObject` inheritance from `LlhttpParser`, drop `HandleCompletion`/`ProcessCommand` overrides
- [x] 2.2 Implement `ProtocolParser` interface: replace three `std::function` constructor params with `OnMessage` callback; implement `GetReadBuffer()` returning `active_chunk_->WritableSpan()` and `OnData(size_t bytes_read)` advancing the cursor and parsing in-place
- [x] 2.3 Remove `Op` enum (no longer needed — parser doesn't handle write completions)
- [x] 2.4 Update `llhttp_parser.cc` to wire up new method signatures; keep chunk management and llhttp integration otherwise unchanged
- [x] 2.5 Update `BUILD.bazel` for `llhttp_parser_lib` — remove dep on `io_object_interface` if no longer needed

## 3. HttpEchoHandler

- [x] 3.1 Create `HttpEchoHandler` in `src/core/io/http_echo_handler.hh` and `.cc` implementing `MessageHandler`
- [x] 3.2 Move HTTP 200 OK response formatting logic from `Connection`'s `on_request` lambda into `HttpEchoHandler::OnMessage`; call `conn.Write(response_bytes)`
- [x] 3.3 Add `HttpEchoHandler` BUILD target depending on `message_handler` and `connection_lib`

## 4. Refactor Connection

- [x] 4.1 Make `Connection` inherit from `event::IOObject`
- [x] 4.2 Add `std::string write_buf_` member field; do NOT add a read buffer (parser provides it)
- [x] 4.3 Implement `HandleCompletion(tag, res, flags)`: tag=Read with res>0 → call `parser_->OnData(res)`, re-arm read via `parser_->GetReadBuffer()` if `Action::NeedMoreData`; tag=Read with res<=0 or tag=Write → call `onEndOfStream()`
- [x] 4.4 Add public `Write(std::span<const std::byte> data)` method: copy data into `write_buf_`, call `Dispatcher::PrepareWrite(this, WriteTag, fd_, write_buf_, 0)`
- [x] 4.5 Change constructor to accept `unique_ptr<ProtocolParser>` + `unique_ptr<MessageHandler>` instead of constructing `LlhttpParser`
- [x] 4.6 Remove `#include "core/io/llhttp_parser.hh"` from `connection.hh`; use forward declarations for parser/handler interfaces
- [x] 4.7 Remove hardcoded HTTP response logic, `LlhttpParser::Op` references, and `response_` member

## 5. Refactor TcpListener

- [x] 5.1 Define a `ConnectionFactory` type alias: `std::function<std::pair<std::unique_ptr<ProtocolParser>, std::unique_ptr<MessageHandler>>(event::DispatcherSharedPtr, int fd, event::IOObject* owner)>`
- [x] 5.2 Add `ConnectionFactory` parameter to `TcpListener` constructor with a default that produces `LlhttpParser` + `HttpEchoHandler`
- [x] 5.3 Update `HandleCompletion` to use the factory when creating connections

## 6. BUILD and Build Verification

- [x] 6.1 Add new BUILD targets: `protocol_parser` and `message_handler` (header-only interface libraries)
- [x] 6.2 Update `connection_lib` deps: remove `llhttp_parser_lib`, add `protocol_parser` and `message_handler` interface libs
- [x] 6.3 Update `tcp_listener_lib` deps to include `http_echo_handler_lib` and `llhttp_parser_lib` (for default factory)
- [x] 6.4 Run `make build` and fix any compilation errors
- [x] 6.5 Run `make test` and verify all tests pass
- [x] 6.6 Run `make clang-tidy` and fix any warnings
