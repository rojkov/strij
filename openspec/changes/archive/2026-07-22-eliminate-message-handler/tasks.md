## 1. Core Infrastructure

- [x] 1.1 Change `ConnectionFactory` in `connection.hh` to return `std::unique_ptr<ProtocolParser>` (not a pair). Remove `MessageHandler` include.
- [x] 1.2 Remove `handler_` member from `Connection` class. Keep only `parser_`.
- [x] 1.3 Update `Connection` constructor in `connection.cc` to only store the parser (no handler).
- [x] 1.4 Remove `:message_handler` dep from `connection_lib` in `src/core/io/BUILD.bazel`.

## 2. Handler Classes

- [x] 2.1 `GatewayTlvHandler`: remove `MessageHandler` inheritance and `OnMessage` override in `gateway_tlv_handler.hh`. Remove `message_handler.hh` include.
- [x] 2.2 `NodeagentTlvHandler`: remove `MessageHandler` inheritance and `OnMessage` override in `nodeagent_tlv_handler.hh`. Remove `message_handler.hh` include.
- [x] 2.3 `GatewayHttpHandler`: remove `MessageHandler` inheritance in `gateway_http_handler.hh`. Rename `OnMessage` to `HandleMessage` (declaration and implementation).
- [x] 2.4 `TrivialEchoHandler`: remove `MessageHandler` inheritance in `trivial_echo_handler.hh`. Rename `OnMessage` to `HandleMessage`. Remove `message_handler.hh` include.
- [x] 2.5 `HttpEchoHandler`: remove `MessageHandler` inheritance in `http_echo_handler.hh`. Rename `OnMessage` to `HandleMessage`. Remove `message_handler.hh` include.

## 3. Factory Sites

- [x] 3.1 Update `gateway.cc`: factory lambda returns `unique_ptr<ProtocolParser>` (not pair). Capture `unique_ptr<GatewayHttpHandler>` in `LlhttpParser` callback. Call `HandleMessage` instead of `OnMessage`.
- [x] 3.2 Update `nodeagent.cc`: factory lambda returns `unique_ptr<ProtocolParser>` (not pair). Capture `unique_ptr<NodeagentTlvHandler>` in `TlvParser` callback.
- [x] 3.3 Update `tcp_connector.cc`: factory lambda returns `unique_ptr<ProtocolParser>` (not pair). Capture `unique_ptr<GatewayTlvHandler>` in `TlvParser` callback.

## 4. Build Cleanup

- [x] 4.1 Remove `:message_handler` target from `src/core/io/BUILD.bazel`.
- [x] 4.2 Remove `:message_handler` dep from `gateway_http_handler_lib`, `http_echo_handler_lib`, `trivial_echo_handler_lib`, `nodeagent_tlv_handler_lib` in BUILD.bazel.
- [x] 4.3 Remove `src/core/io/message_handler.hh` file.

## 5. Verify

- [x] 5.1 Run `make build` — all targets compile.
- [x] 5.2 Run `make test` — all tests pass.
