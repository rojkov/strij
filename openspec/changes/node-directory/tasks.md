## 1. Dispatcher: Add PrepareConnect

- [x] 1.1 Add `PrepareConnect` pure virtual method to `include/carrot/event/dispatcher.hh`
- [x] 1.2 Declare `PrepareConnect` override in `src/core/event/dispatcher_impl.hh`
- [x] 1.3 Implement `PrepareConnect` in `src/core/event/dispatcher_impl.cc` using `io_uring_prep_connect`

## 2. Node class

- [x] 2.1 Create `src/core/io/node.hh` with `Node` class: `IOObject` subclass, `Status` enum, `StartConnect()`, `HandleCompletion()`, `ProcessCommand()`, `GetConnection()`, `IsAvailable()`, `GetAddress()`
- [x] 2.2 Create `src/core/io/node.cc` with `Node` implementation: socket creation, `PrepareConnect` submission, connect completion handling (create `Connection` on success, close fd on failure), `CLOSE_CONNECTION` command handling
- [x] 2.3 Add `carrot_cc_library` target `node_lib` in `src/core/io/BUILD.bazel`

## 3. NodeDirectory class

- [x] 3.1 Create `src/core/io/node_directory.hh` with `NodeDirectory` class: owns `vector<unique_ptr<Node>>`, `StartConnectAll()`, `GetNextNode()`, `GetNodeCount()`, `GetAvailableCount()`
- [x] 3.2 Create `src/core/io/node_directory.cc` with `NodeDirectory` implementation: address parsing, node creation, round-robin selection among available nodes
- [x] 3.3 Add `carrot_cc_library` target `node_directory_lib` in `src/core/io/BUILD.bazel`

## 4. Update GatewayHttpHandler

- [x] 4.1 Change `GatewayHttpHandler` constructor in `src/core/io/gateway_http_handler.hh` from `vector<Connection*>&` to `NodeDirectory&`
- [x] 4.2 Update `HandleMessage` in `src/core/io/gateway_http_handler.cc` to use `node_directory_.GetNextNode()` instead of direct vector indexing

## 5. Update gateway entrypoint

- [x] 5.1 Replace `TcpConnector` with `NodeDirectory` in `src/exe/gateway/gateway.cc`
- [x] 5.2 Pass `ConnectionFactory` lambda to `NodeDirectory` constructor
- [x] 5.3 Call `node_directory.StartConnectAll()` before `dispatcher->Run()`

## 6. Remove TcpConnector

- [x] 6.1 Delete `src/core/io/tcp_connector.hh` and `src/core/io/tcp_connector.cc`
- [x] 6.2 Remove `tcp_connector_lib` target from `src/core/io/BUILD.bazel`
- [x] 6.3 Remove `tcp_connector_lib` dependency from `src/exe/gateway/BUILD.bazel`

## 7. Build and verify

- [x] 7.1 Run `make build` and fix any compilation errors
- [x] 7.2 Run `make test` and verify existing tests pass
