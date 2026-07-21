## 1. TLV Frame Types and Parser

- [x] 1.1 Define `TlvFrame` struct with `type_id` and `value` fields and type constants (`kTaskSubmission`, `kResult`, `kHeartbeat`) in `src/core/io/tlv_frame.hh`
- [x] 1.2 Update `TlvParser` callback type from `std::function<void(std::span<const std::byte>)>` to `std::function<void(TlvFrame)>` in `tlv_parser.hh`
- [x] 1.3 Update `TlvParser::OnData` to deliver `TlvFrame{type_id, value}` with the full value span (parser does not interpret value content)
- [x] 1.4 Update `TlvParser` tests in `test/core/io/tlv_parser_test.cc` to verify TlvFrame delivery with correct type_id and value

## 2. Nodeagent TLV Handler

- [x] 2.1 Create `NodeagentTlvHandler` class that receives `TlvFrame`, extracts task_id from value (first 8 bytes), and echoes back a `TlvFrame{type_id=kResult}` with the same task_id + payload in value
- [x] 2.2 Wire `NodeagentTlvHandler` into nodeagent entrypoint (`nodeagent.cc`), replacing `TrivialEchoHandler`
- [x] 2.3 Update nodeagent `TcpListener` factory to produce `TlvParser` with `TlvFrame` callback + `NodeagentTlvHandler`

## 3. Gateway Infrastructure

- [x] 3.1 Create `ResultReceiverStorage` class with `put(task_id, receiver)`, `get(task_id)`, `erase(task_id)` methods in `src/core/io/result_receiver_storage.hh`
- [x] 3.2 Define `ResultReceiver` base class with virtual `Deliver(std::span<const std::byte> value)` method
- [x] 3.3 Create `EchoResultReceiver` that stores the original `Connection&` and writes the result body back as an HTTP response on `Deliver()`

## 4. Gateway Handlers

- [x] 4.1 Create `GatewayHttpHandler` class with `OnMessage(HttpRequest, Connection&)` that generates task_ids, selects nodeagent round-robin, stores receivers, and submits TLV tasks (task_id + payload in value)
- [x] 4.2 Create `GatewayTlvHandler` class with `OnMessage(TlvFrame, Connection&)` that dispatches on type_id (Result → extract task_id from value, lookup receiver, deliver payload; Heartbeat → log)
- [x] 4.3 Implement `TlvSender` class wrapping a connected socket with `SendFrame(TlvFrame)` method (serializes type_id + length + value to wire)

## 5. Gateway Wiring

- [x] 5.1 Create nodeagent connection setup in `gateway.cc`: connect to configured addresses, create `TlvSender` for each, pass to `GatewayHttpHandler`
- [x] 5.2 Wire `GatewayHttpHandler` into gateway HTTP listener factory with `LlhttpParser` and `ResultReceiverStorage`
- [x] 5.3 Wire `GatewayTlvHandler` into gateway TLV listener (or per-nodeagent connection handler) with shared `ResultReceiverStorage`
- [x] 5.4 Return 503 Service Unavailable if no nodeagent connections are available

## 6. Integration and Testing

- [x] 6.1 Add unit tests for `TlvSender` frame serialization
- [x] 6.2 Add unit tests for `GatewayHttpHandler` task creation and TLV submission
- [x] 6.3 Add unit tests for `GatewayTlvHandler` result dispatch and receiver lookup
- [x] 6.4 Add integration test: start gateway + nodeagent, send HTTP request, verify echo response
- [x] 6.5 Run `make build` and `make test` to verify everything compiles and passes
