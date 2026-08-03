## 1. Serialization

- [x] 1.1 Add `SerializeTlvFrame()` free function declaration to `tlv_frame.hh`.
- [x] 1.2 Implement `SerializeTlvFrame()` in new `tlv_frame.cc`: build `[type_id:1][length:4 net-order][value:N]`.
- [x] 1.3 Add `tlv_frame.cc` to the `:tlv_frame` target in `src/core/io/BUILD.bazel`.
- [x] 1.4 Refactor `NodeagentTlvHandler::HandleFrame()` to use `SerializeTlvFrame()` instead of hand-rolled serialization.

## 2. Connection partial writes

- [x] 2.1 Add `size_t write_offset_{0}` member to `Connection` in `connection.hh`.
- [x] 2.2 Update `Connection::Write()` to reset `write_offset_` to 0 and assert `write_buf_.empty()`.
- [x] 2.3 Implement partial write handling in `Connection::HandleCompletion(kWrite)`: on short write, advance `write_offset_` and resubmit remaining span; on completion or error, clear buffer and reset offset.

## 3. TcpConnector returns Connection*

- [x] 3.1 Change `TcpConnector::Connect()` return type from `int` to `Connection*` in `tcp_connector.hh`.
- [x] 3.2 Update `TcpConnector::Connect()` implementation in `tcp_connector.cc` to return `connections_.back().get()` instead of `fd`.

## 4. GatewayHttpHandler uses Connection*

- [x] 4.1 Remove `TlvSender` class and `TlvSenderPtr` alias from `gateway_http_handler.hh`.
- [x] 4.2 Change `GatewayHttpHandler` constructor to accept `std::vector<Connection*>&` instead of `std::vector<TlvSenderPtr>&`.
- [x] 4.3 Update `GatewayHttpHandler::HandleMessage()` to serialize the TLV frame via `SerializeTlvFrame()` and call `connection->Write(frame)`.
- [x] 4.4 Remove TlvSender-related includes from `gateway_http_handler.hh/.cc`.

## 5. Gateway entrypoint

- [x] 5.1 Update `gateway.cc`: store `std::vector<strij::io::Connection*>` instead of `std::vector<strij::io::TlvSenderPtr>`.
- [x] 5.2 Update `gateway.cc`: call `connector.Connect()` and store the returned `Connection*`.
- [x] 5.3 Update `gateway.cc`: pass `nodeagent_conns` to `GatewayHttpHandler` constructor.

## 6. Tests

- [x] 6.1 Rewrite `TlvSenderTest` as `SerializeTlvFrameTest`: test the serialization function directly (no socket needed).
- [x] 6.2 Verify `NodeagentTlvHandlerTest` tests still pass with the refactored serialization.
- [x] 6.3 Add a test for `Connection` partial write handling (submit a write larger than the peer's receive buffer, verify all bytes arrive).

## 7. Verify

- [x] 7.1 Run `make build` — all targets compile.
- [x] 7.2 Run `make test` — all tests pass.
