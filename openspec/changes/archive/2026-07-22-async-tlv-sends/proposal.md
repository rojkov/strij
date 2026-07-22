## Why

`TlvSender::SendFrame()` uses a blocking `::write(fd_, ...)` syscall on the event loop thread (`gateway_http_handler.cc:35`). This bypasses io_uring entirely and blocks the entire event loop if the nodeagent's TCP send buffer is full. It also shares a raw fd with a `Connection` object that simultaneously reads from the same socket via io_uring — two objects, one fd, no coordination.

Meanwhile, the nodeagent side already uses `Connection::Write()` for async sends (`nodeagent_tlv_handler.cc:26`). Both sides build the same TLV wire format, but only the gateway side uses a synchronous syscall.

## What Changes

- **BREAKING**: Delete `TlvSender` class and `TlvSenderPtr` type alias (`gateway_http_handler.hh`).
- **BREAKING**: `TcpConnector::Connect()` returns `Connection*` instead of `int`, giving callers access to the async-capable connection object.
- **BREAKING**: `GatewayHttpHandler` takes `std::vector<Connection*>&` instead of `std::vector<TlvSenderPtr>&`, and writes TLV frames via `Connection::Write()`.
- Add `SerializeTlvFrame(uint8_t type_id, std::span<const std::byte> value) -> std::vector<std::byte>` free function to `tlv_frame.hh`, extracting the duplicated wire format serialization from both `TlvSender` and `NodeagentTlvHandler`.
- `Connection::Write()` gains partial write handling via a `write_offset_` cursor, resubmitting remaining bytes on short writes.
- `NodeagentTlvHandler` uses `SerializeTlvFrame()` instead of hand-rolled serialization.
- `gateway.cc` stores `Connection*` pointers instead of `TlvSenderPtr`.

## Capabilities

### New Capabilities

_(none)_

### Modified Capabilities

- `connection-io-object`: `Connection::Write()` gains partial write handling (`write_offset_` member, resubmission logic in `HandleCompletion(kWrite)`).
- `gateway-task-bridge`: `TlvSender` is removed. `GatewayHttpHandler` writes TLV frames via `Connection::Write()` using `SerializeTlvFrame()`. `TcpConnector::Connect()` returns `Connection*`.
- `typed-tlv-messages`: `TlvFrame` gains a `SerializeTlvFrame()` free function for wire format serialization.

## Impact

- **Files deleted**: _(none — TlvSender code is removed from `gateway_http_handler.hh/.cc`, but the files remain with `GatewayHttpHandler`)_
- **Files changed**: `tlv_frame.hh`, `tlv_frame.cc` (new), `connection.hh`, `connection.cc`, `gateway_http_handler.hh`, `gateway_http_handler.cc`, `nodeagent_tlv_handler.cc`, `tcp_connector.hh`, `tcp_connector.cc`, `gateway.cc`, `gateway_test.cc`, `BUILD.bazel` (2 targets)
- **Files unchanged**: `protocol_parser.hh`, `tlv_parser.*`, `llhttp_parser.*`, `echo_result_receiver.*`, `result_receiver_storage.*`, `gateway_tlv_handler.*`, `dispatcher_impl.*`
- **Bazel targets**: `:tlv_frame` gains a new `.cc` source; `:gateway_http_handler_lib` loses `TlvSender` code; no new targets
- **External deps**: none affected
