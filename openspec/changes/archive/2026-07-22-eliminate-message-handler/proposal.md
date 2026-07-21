## Why

`MessageHandler` is a polymorphic interface (`OnMessage(span<byte>, Connection&)`) that all handlers must inherit from. But TLV-based handlers (`GatewayTlvHandler`, `NodeagentTlvHandler`) bypass it entirely — their parser callbacks call `HandleFrame(TlvFrame, Connection&)` directly, and `OnMessage()` is a no-op stub. `Connection` stores a `unique_ptr<MessageHandler>` that is never used for TLV connections — it exists solely to keep the handler alive. The interface conflates two unrelated handler types (HTTP byte-level vs TLV frame-level) and forces dead vtable indirection on the TLV path.

## What Changes

- **BREAKING**: Delete `MessageHandler` abstract class and `MessageHandlerPtr` type alias (`message_handler.hh`).
- **BREAKING**: Change `ConnectionFactory` from returning `pair<ProtocolParser, MessageHandler>` to returning only `unique_ptr<ProtocolParser>`.
- **BREAKING**: Remove `handler_` member from `Connection` — connection owns only the parser.
- Handler classes (`GatewayTlvHandler`, `NodeagentTlvHandler`, `GatewayHttpHandler`, `TrivialEchoHandler`, `HttpEchoHandler`) become standalone concrete classes with no base class.
- Handler ownership transfers to the parser's callback lambda (captured as `unique_ptr`).
- Delete `:message_handler` Bazel target and remove its deps from all handler libraries.

## Capabilities

### New Capabilities

_(none)_

### Modified Capabilities

- `message-handler`: The `MessageHandler` abstract interface requirement is being removed. The `HttpEchoHandler` requirement changes to a standalone class (no interface conformance). The capability is effectively dissolved — its remaining behavior (HTTP echo) is covered by the concrete class.
- `connection-io-object`: The `ConnectionFactory` type, Connection ownership model, and TcpListener factory requirements change. Connection no longer owns a handler; the factory returns a single parser.

## Impact

- **Files deleted**: `src/core/io/message_handler.hh`
- **Files changed**: `connection.hh`, `connection.cc`, all 5 handler headers, `gateway.cc`, `nodeagent.cc`, `tcp_connector.cc`, `BUILD.bazel`
- **Files unchanged**: `protocol_parser.hh`, `tlv_parser.*`, `llhttp_parser.*`, `tlv_frame.*`, `connection.hh` (write path), all tests
- **Bazel targets**: `:message_handler` deleted; deps removed from `gateway_http_handler_lib`, `http_echo_handler_lib`, `trivial_echo_handler_lib`, `nodeagent_tlv_handler_lib`, `connection_lib`
- **External deps**: none affected
