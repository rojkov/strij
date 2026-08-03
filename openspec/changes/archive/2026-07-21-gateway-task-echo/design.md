## Context

The strij gateway server currently has two independent echo servers: `gateway` (HTTP on port 8081) and `nodeagent` (TLV on port 9090). Both use the same `ProtocolParser` → `MessageHandler` pipeline, but parsers discard protocol metadata (HTTP headers, TLV type_id) before delivering raw bytes to handlers.

The gateway needs to become a protocol bridge: receive HTTP requests containing computation tasks, forward them to nodeagents via TLV, and deliver results back as HTTP replies. The TLV wire format needs a task_id for correlation. Both parsers need to deliver structured data instead of raw bytes.

This design covers the simple echo phase: nodeagent echoes task bodies back, gateway routes them to the originating HTTP connection. It validates the typed message delivery and correlation architecture before adding streaming or result endpoints.

## Goals / Non-Goals

**Goals:**
- TLV parser delivers `TlvFrame{type_id, value}` instead of raw bytes
- TLV wire format: task_id is part of the value for task-related frames, not a frame-level field
- Gateway HTTP handler creates tasks, selects nodeagents round-robin, stores result receivers
- Gateway TLV handler dispatches by type_id, correlates results to HTTP connections
- Nodeagent TLV handler echoes task bodies back as TLV result frames
- Gateway pre-establishes TLV connections to nodeagents on startup
- ResultReceiverStorage shared between gateway's HTTP and TLV handlers

**Non-Goals:**
- HTTP header parsing (result endpoint support) — future work
- Streaming result delivery — future work
- Heartbeat implementation — type_id reserved but not acted on
- Protocol negotiation or multiplexing on a single port
- Modifying the base `MessageHandler` or `ProtocolParser` interfaces
- Modifying the generic `Connection` class

## Decisions

### D1: Typed callbacks in parsers, not polymorphic messages

**Decision:** Parsers deliver typed structs via protocol-specific `std::function` callbacks. `TlvParser` delivers `TlvFrame`, `LlhttpParser` delivers `HttpRequest` (future). The base `ProtocolParser` keeps its `span<byte>` callback for backward compatibility.

**Why not `std::variant` or `std::any`:** No runtime type dispatch overhead. Each parser-handler pair is statically bound at construction. The handler knows its message type at compile time.

**Why not evolve `MessageHandler::OnMessage`:** The base class stays generic for simple echo handlers. Gateway handlers are concrete classes with their own `OnMessage` signatures. This avoids breaking existing code and keeps the abstraction clean.

### D2: Task_id is value content, not frame metadata

**Decision:** `TlvFrame` contains only `{type_id, value}`. Task_id lives inside the value for task-related frames: `[task_id: 8 bytes][payload: N bytes]`. Consumers (handlers) extract task_id from value themselves. The parser does not interpret value content.

**Why not frame-level task_id:** Heartbeat frames have no task_id. Putting it in the struct forces an unused field for non-task messages. Keeping it in value lets each consumer parse only what it needs.

**Why 8 bytes:** `uint64_t` provides 2^64 unique task IDs, sufficient for any practical workload. Aligned read is cheap on x86_64.

### D3: Gateway owns ResultReceiverStorage, injected into handlers

**Decision:** `ResultReceiverStorage` is created in `gateway.cc::main()` and passed by reference to both `GatewayHttpHandler` and `GatewayTlvHandler`.

**Why not HTTP handler owns it:** The storage is infrastructure, not application logic. Both handlers need equal access. Main owns the lifecycle cleanly.

### D4: Gateway initiates TLV connections to nodeagents

**Decision:** Gateway calls `connect()` to nodeagent addresses on startup. Nodeagent listens. Gateway holds `TlvSender` objects for each connection.

**Why gateway initiates:** Simpler startup ordering. Gateway knows nodeagent addresses (config). Nodeagent doesn't need to discover gateway.

**Future alternative:** Nodeagent registers with gateway. Only the connection direction changes; `TlvConnection` is symmetric.

### D5: Protocol-specific handlers, not generic `MessageHandler`

**Decision:** `GatewayHttpHandler` and `GatewayTlvHandler` are concrete classes, not derived from `MessageHandler`. The `TlvConnection` (or `TlvSender`) wraps parser + write path. The generic `Connection` class and `ConnectionFactory` remain for simple echo use cases.

**Why:** Gateway handlers need access to shared state (`ResultReceiverStorage`) and cross-connection capabilities (HTTP handler writes to TLV connections). The generic `Connection` only provides `Write()` to the current connection. Protocol-specific handlers give full control.

### D6: Nodeagent handler echoes TLV frames with type_id=Result

**Decision:** `NodeagentTlvHandler` receives a `TlvFrame` (type_id=TaskSubmission), extracts task_id and payload from the value, and sends back a `TlvFrame` with type_id=Result and the same task_id + payload in the value.

**Simple echo:** No processing, no state. Validates the full round-trip.

## Risks / Trade-offs

- **[Risk] TLV parser callback type change breaks existing `TlvParser` users** → Mitigation: `TrivialEchoHandler` in nodeagent is replaced by `NodeagentTlvHandler`. No other users exist. The generic `ConnectionFactory` path is unaffected if we keep a `span<byte>` overload or adapter.

- **[Risk] Consumer extracts task_id from value and payload is < 8 bytes** → Mitigation: Handlers validate minimum value length before extracting task_id. Frames with undersized values for task types are logged and discarded by the handler.

- **[Risk] ResultReceiverStorage grows unbounded if tasks are never completed** → Mitigation: For simple echo this is unlikely. Future work adds timeouts and cleanup.

- **[Trade-off] Duplicating connection logic for TLV vs generic Connection** → Accepted: The generic `Connection` is designed for simple request-response. Gateway's cross-protocol routing needs richer capabilities. The duplication is small (TLV read/write loop) and the clarity is worth it.
