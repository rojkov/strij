## Why

Gateway and nodeagent need to exchange computation tasks and results. Currently both `gateway` (HTTP) and `nodeagent` (TLV) are simple echo servers. The gateway needs to become a protocol bridge: receive HTTP requests containing tasks, forward them to nodeagents via TLV, and deliver the results back to HTTP clients. The TLV parser currently discards the type_id, making it impossible to distinguish message types (task result vs heartbeat). A correlation mechanism is needed to route TLV results back to the originating HTTP connections.

This is the first step toward the full gateway architecture. The goal is a simple echo: nodeagent bounces task bodies back, gateway delivers them as HTTP replies. This validates the typed message delivery, task correlation, and cross-protocol routing without the complexity of streaming or result endpoints.

## What Changes

- **TLV parser delivers typed frames**: `TlvParser` now produces `TlvFrame` structs containing `type_id` and `value` instead of raw bytes. Task-related frames carry task_id inside the value (first 8 bytes).
- **TLV wire format**: Standard `[type_id][length][value]` format. Task-related frames encode `[task_id: 8 bytes][payload]` in the value. Heartbeat frames have no task_id.
- **Gateway HTTP handler**: Replaces `HttpEchoHandler`. Creates tasks from HTTP request bodies, selects a nodeagent via round-robin, stores a result receiver, and submits the task as a TLV frame.
- **Gateway TLV handler**: Dispatches incoming TLV frames by type_id (result, heartbeat). Looks up the result receiver by task_id and delivers the result.
- **ResultReceiverStorage**: Shared state mapping task_ids to result receivers. Owned by gateway's main, injected into both HTTP and TLV handlers.
- **Nodeagent TLV handler**: Replaces `TrivialEchoHandler`. Receives TLV task frames, echoes the body back as a TLV result frame.
- **Gateway connects to nodeagent**: Gateway initiates TLV connections to nodeagents on startup (pre-established connections).
- **Simple echo flow**: HTTP request body → TLV task → nodeagent echoes → TLV result → HTTP reply with body.

## Capabilities

### New Capabilities
- `typed-tlv-messages`: TlvParser delivers `TlvFrame{type_id, value}` structs. Defines the wire format, the `TlvFrame` struct, and the updated parser callback type. Task_id is value content, not frame metadata.
- `gateway-task-bridge`: Gateway's HTTP handler, TLV handler, ResultReceiverStorage, and nodeagent echo handler. Covers task creation, round-robin nodeagent selection, result correlation, and the simple echo flow.

### Modified Capabilities

## Impact

- **TlvParser** (`src/core/io/tlv_parser.hh`, `src/core/io/tlv_parser.cc`): Callback type changes from `std::function<void(std::span<const std::byte>)>` to `std::function<void(TlvFrame)>`. Parser sets type_id and value from the wire format.
- **TrivialEchoHandler** (`src/core/io/trivial_echo_handler.hh`, `src/core/io/trivial_echo_handler.cc`): Replaced by `NodeagentTlvHandler` in nodeagent context.
- **HttpEchoHandler** (`src/core/io/http_echo_handler.hh`, `src/core/io/http_echo_handler.cc`): Replaced by `GatewayHttpHandler` in gateway context. `HttpEchoHandler` may be kept for backward compatibility.
- **gateway.cc** (`src/exe/gateway/gateway.cc`): New entrypoint wiring gateway handlers, ResultReceiverStorage, and nodeagent connections.
- **nodeagent.cc** (`src/exe/nodeagent/nodeagent.cc`): Updated to use `NodeagentTlvHandler`.
- **New files**: `TlvFrame` struct, `GatewayHttpHandler`, `GatewayTlvHandler`, `NodeagentTlvHandler`, `ResultReceiverStorage`, `TlvSender`/`TlvConnector`.
- **Dependencies**: None added. Uses existing io_uring infrastructure and llhttp.
