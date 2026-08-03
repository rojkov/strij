## Why

`TcpConnector::Connect()` calls the synchronous `connect()` syscall, blocking the event loop thread during the TCP handshake. While this currently only happens at startup (before `dispatcher->Run()`), it violates the project's zero-synchronous-syscalls principle and blocks future work: a discovery mechanism for computing nodes and a pluggable scheduler require nodes to be modeled as first-class objects with status tracking, not raw `Connection*` pointers in a vector.

## What Changes

- **NEW**: `NodeDirectory` class — initialized with a list of node addresses (`"host:port"`) and a `ConnectionFactory`. Replaces `TcpConnector`. Manages a collection of `Node` instances and provides `GetNextNode()` for round-robin selection among available nodes.
- **NEW**: `Node` class — an `IOObject` that owns a `Connection` and tracks status (`kInitial` → `kConnecting` → `kConnected` | `kDisconnected`). Handles async connect completion via io_uring. Created by `NodeDirectory`, one per configured address.
- **NEW**: `Dispatcher::PrepareConnect()` — adds `io_uring_prep_connect` to the Dispatcher interface, completing the non-blocking I/O surface alongside `PrepareRead`, `PrepareWrite`, and `PrepareAcceptMultishot`.
- **BREAKING**: `GatewayHttpHandler` constructor changes from `vector<Connection*>&` to `NodeDirectory&`. Task routing uses `node_directory_.GetNextNode()` instead of direct index into a vector.
- **BREAKING**: `gateway.cc` replaces `TcpConnector` with `NodeDirectory`. Connection factory is passed to `NodeDirectory` instead of being created inline in `TcpConnector::Connect()`.
- **REMOVED**: `TcpConnector` class (both `.hh` and `.cc`).

## Capabilities

### New Capabilities

- `node-directory`: NodeDirectory and Node abstractions — managing node lifecycle, async connection, and status tracking.
- `dispatcher-connect`: Adding `PrepareConnect` to the `Dispatcher` interface and implementing it in `DispatcherImpl` via `io_uring_prep_connect`.

### Modified Capabilities

- `gateway-task-bridge`: The requirement "Gateway pre-establishes TLV connections to nodeagents" changes mechanism from `TcpConnector` to `NodeDirectory`. The `GatewayHttpHandler` no longer holds `vector<Connection*>&` but queries `NodeDirectory` for available nodes. The 503 response behavior is preserved.

## Impact

- **Files removed**: `src/core/io/tcp_connector.hh`, `src/core/io/tcp_connector.cc`
- **Files added**: `src/core/io/node_directory.hh`, `src/core/io/node_directory.cc`, `src/core/io/node.hh`, `src/core/io/node.cc`
- **Files modified**: `include/strij/event/dispatcher.hh`, `src/core/event/dispatcher_impl.hh`, `src/core/event/dispatcher_impl.cc`, `src/core/io/gateway_http_handler.hh`, `src/core/io/gateway_http_handler.cc`, `src/exe/gateway/gateway.cc`, `src/core/io/BUILD.bazel`, `src/exe/gateway/BUILD.bazel`
- **External dependencies**: None new — `io_uring_prep_connect` is already available in the bundled liburing.
