## Why

`Connection` is hard-wired to `LlhttpParser` — it directly constructs the parser, references its `Op` enum, and embeds HTTP-specific response logic. This makes it impossible to reuse `Connection` with an alternative protocol parser (e.g. TLV framing) without modifying the class itself. Decoupling enables pluggable protocol support while correcting an architectural inversion where the parser acts as the IOObject despite not owning the I/O.

## What Changes

- **New abstract `ProtocolParser` interface** that all parsers implement. Parsers own their read buffers (zero-copy from io_uring) and expose them via `GetReadBuffer()`. They process data in-place via `OnData(size_t)` and return an action (`NeedMoreData` or `MessageComplete`). They do not inherit from `IOObject`.
- **`Connection` becomes an `IOObject`** and owns only the write buffer (`write_buf_`). It directly receives read/write completions from the Dispatcher, drives the read loop using the parser's buffers, and delegates message handling to the handler. Read buffers are owned by the parser — the io_uring lifetime contract is satisfied by the ownership chain (Connection → parser → buffer).
- **`LlhttpParser` drops `IOObject` inheritance.** Its existing `Chunk` system becomes the read buffer provider via `GetReadBuffer()`. Data is processed in-place via `OnData(size_t)` — no copying.
- **New `MessageHandler` interface** abstracting what happens when a complete message arrives. HTTP echo logic moves to an `HttpEchoHandler` implementation.
- **`TcpListener` creates connections via a configurable parser/handler factory** instead of hardcoding HTTP.

## Capabilities

### New Capabilities
- `protocol-parser`: Abstract parser interface (`ProtocolParser`) — parsers own read buffers, expose them via `GetReadBuffer()`, process data in-place via `OnData(size_t)`.
- `message-handler`: Abstract message handling interface (`MessageHandler`) and the `HttpEchoHandler` implementation.
- `connection-io-object`: `Connection` as an `IOObject` managing all read/write completions directly.

### Modified Capabilities

## Impact

- **Files modified:** `connection.hh`, `connection.cc`, `llhttp_parser.hh`, `llhttp_parser.cc`, `tcp_listener.hh`, `tcp_listener.cc`, `BUILD.bazel`
- **New files:** `protocol_parser.hh`, `message_handler.hh`, `http_echo_handler.hh`, `http_echo_handler.cc` (exact names TBD in design)
- **Dependencies:** No new external dependencies. `llhttp` dependency stays in the parser library.
- **API:** `Connection` constructor signature changes (takes parser factory + handler). `TcpListener` constructor may gain a factory parameter.
- **Tests:** Existing `log_frontend_test` unaffected. No existing Connection/parser tests to break. New parsers can be tested independently.
