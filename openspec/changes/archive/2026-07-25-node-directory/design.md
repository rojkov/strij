## Context

The gateway currently uses `TcpConnector` to establish TCP connections to nodeagents. `TcpConnector::Connect()` calls the synchronous `connect()` syscall, blocking the event loop thread. The gateway creates connections at startup (before `dispatcher->Run()`), stores raw `Connection*` pointers in a `vector`, and passes this vector by reference to `GatewayHttpHandler` for round-robin task routing.

The event loop (`DispatcherImpl`) wraps io_uring and provides `PrepareRead`, `PrepareWrite`, and `PrepareAcceptMultishot` — but no `PrepareConnect`. The `connect()` syscall is the only blocking network operation in the codebase.

Future work requires nodes to be first-class objects: a discovery mechanism will add/remove nodes at runtime, and a scheduler will route tasks based on node capabilities and load. Raw `Connection*` pointers cannot support this.

## Goals / Non-Goals

**Goals:**
- Eliminate the synchronous `connect()` syscall by adding `PrepareConnect` to the `Dispatcher` interface
- Introduce `Node` as an `IOObject` that owns a `Connection` and tracks status (`kInitial` → `kConnecting` → `kConnected` | `kDisconnected`)
- Introduce `NodeDirectory` as the container that manages `Node` instances and provides round-robin selection among available nodes
- Replace `TcpConnector` entirely — remove the class and update all callers
- Preserve existing behavior: 503 when no nodes are available, round-robin scheduling, echo flow

**Non-Goals:**
- Reconnection logic (disconnected nodes stay disconnected — TODO for future)
- Scheduler abstraction (round-robin is hardcoded in `NodeDirectory::GetNextNode()`)
- Node capabilities or load tracking (fields can be added to `Node` later)
- Dynamic node discovery at runtime (addresses are provided at construction time)

## Decisions

### D1: Node is an IOObject

**Choice:** `Node` inherits from `event::IOObject` and handles its own connect completion via `HandleCompletion`.

**Alternatives considered:**
- *NodeDirectory handles all completions with per-node tags*: Rejected because the 3-bit tag space (8 tags) limits the number of nodes to 8. `NodeDirectory` could use more bits, but that would require changing the tag encoding scheme across the entire codebase.
- *Node is not an IOObject; connect is handled by a callback lambda submitted to the ring*: Rejected because it would require a different ownership model and would not fit the existing `IOObject` pattern used by `Connection`, `TcpListener`, and `DispatcherImpl`.

**Rationale:** Each `Node` independently owns its connect lifecycle. Making it an `IOObject` is consistent with the existing pattern where `Connection` handles read/write completions and `TcpListener` handles accept completions. The tag space is per-object, so there is no global limit on the number of nodes.

### D2: Connection is created after connect succeeds

**Choice:** `Node::HandleCompletion(kConnect, res)` creates the `Connection` only when `res == 0`. The `Connection` constructor immediately calls `PrepareRead`, so the fd must be connected before construction.

**Alternatives considered:**
- *Create Connection before connect, pause reads until connected*: Rejected because `Connection` has no concept of "paused" state — its constructor unconditionally calls `PrepareRead`. Adding this state would complicate `Connection` for no benefit.

**Rationale:** Keeping `Connection` unchanged is simpler. The `Node` holds the `fd_` until connect completes, then transfers ownership to the `Connection`.

### D3: NodeDirectory is not an IOObject

**Choice:** `NodeDirectory` is a plain class that owns `Node` instances and provides `GetNextNode()`. It does not inherit from `IOObject`.

**Rationale:** `NodeDirectory` does not handle any io_uring completions directly — each `Node` handles its own. `NodeDirectory` is a container and coordinator, not an event loop participant. This keeps the separation of concerns clean.

### D4: PrepareConnect is a new Dispatcher method

**Choice:** Add `virtual void PrepareConnect(IOObject*, uint8_t tag, int fd, const struct sockaddr*, socklen_t) PURE` to the `Dispatcher` interface, implemented in `DispatcherImpl` via `io_uring_prep_connect`.

**Alternatives considered:**
- *Expose the io_uring ring directly*: Rejected because it would break the abstraction layer — `Dispatcher` is the interface, `io_uring` is the implementation detail.
- *Add a generic SubmitSqe method*: Rejected because it would leak io_uring concepts into the `Dispatcher` interface.

**Rationale:** `PrepareConnect` follows the exact same pattern as `PrepareRead`, `PrepareWrite`, and `PrepareAcceptMultishot`. It is the natural extension of the existing interface.

### D5: NodeDirectory::GetNextNode() does round-robin among available nodes

**Choice:** `GetNextNode()` iterates from `next_node_index_` and returns the next `Node` where `IsAvailable()` is true. If no node is available, it returns `nullptr`.

**Rationale:** This preserves the existing round-robin behavior from `GatewayHttpHandler` while skipping unavailable nodes. When a `Scheduler` is introduced later, `GetNextNode()` can delegate to it.

### D6: Node handles CLOSE_CONNECTION from its Connection

**Choice:** `Node` implements `ProcessCommand` to handle `CLOSE_CONNECTION` from its owned `Connection`. On close, `Node` resets its `connection_` and transitions to `kDisconnected`.

**Rationale:** This follows the same ownership pattern as `TcpListener` and the former `TcpConnector` — the owner of a `Connection` handles its lifecycle events via `ProcessCommand`.

## Risks / Trade-offs

- **Startup latency with async connect**: Currently `connect()` blocks and returns immediately on success (or throws). With async connect, the event loop must run for the connects to complete. HTTP requests arriving before nodes connect will get 503. This is acceptable — the same behavior exists today when nodeagents are unreachable.

- **Single-threaded constraint**: `Node`, `NodeDirectory`, and `GatewayHttpHandler` all run on the dispatcher thread. No thread-safety mechanisms are needed, but this also means a slow connect completion blocks task routing for all nodes. This is acceptable for the current architecture.

- **No reconnection**: If a node disconnects, it stays disconnected. The `GatewayHttpHandler` will skip it in round-robin. This is explicitly out of scope but documented as a TODO in the `Node` declaration.
