## Why

Today both ends of the wire hardcode a single task-flow path: `GatewayHttpHandler` calls one `Scheduler::Choose()` and then pushes the task (`gateway_http_handler.cc:100,126`), and `NodeagentTlvHandler::HandleFrame` admits and runs every `kTaskSubmission` inline. This cannot express probe-based scheduling (power-of-two choices, deferred admission, pull/grant races), data-dependency gating, or locally-originated child tasks — all required for a distributed runtime where gateways cannot track cluster-wide resource state. Scheduling must become a pluggable, bidirectional protocol owned by extensions on both ends of the wire, with a single shared contract.

## What Changes

This change (Phase 0) introduces the mechanism and preserves current behavior. Future protocol extensions (probe, data deps, child tasks) are designed for but not implemented here.

- **Unified `Scheduler` interface** shared by gateway and nodeagent extensions: `Schedule(const Task&, ResultReceiverPtr)` (fire-and-forget), `RequiredProtocol()`, `HandleFrame(TlvFrame, Connection&)` (default no-op), `HandledFrameTypes()`. **BREAKING**: replaces gateway `Scheduler::Choose(NodeDirectory&, TaskOffer) -> Node*`; `Choose` becomes a private helper of push-style schedulers.
- **Split `FactoryContext`** into `GatewayFactoryContext` (Dispatcher, Logger, NodeDirectory, ResultReceiverStorage) and `NodeagentFactoryContext` (Dispatcher, Logger, FunctionResolver, AdmissionController, RunTask service), so no service leaks across sides. **BREAKING**: every extension factory `Create(config, context)` re-binds to its side's context type; the old `FactoryContext`/`FactoryContextImpl` retire.
- **Split the scheduler factory category** into `GatewaySchedulerFactory` (registered in `Registry<GatewaySchedulerFactory>`) and `NodeSchedulerFactory` (registered in `Registry<NodeSchedulerFactory>`), both returning `std::unique_ptr<Scheduler>`. **BREAKING**: registration macro targets change.
- **Gateway: per-type scheduling.** `GatewayConfig.scheduler` becomes `repeated schedulers` with task-type matching plus an optional default. A router dispatches `Schedule` by task type; `GatewayHttpHandler` calls `Schedule` and stops owning node I/O; inbound node frames route to the owning scheduler's `HandleFrame`. `round_robin` and `capability_aware` reimplement as `Schedule` (choose + send internally), preserving behavior.
- **Nodeagent: node-local-scheduler extension category.** Initial "push" local scheduler preserves current behavior exactly: `kTaskSubmission` → admission → task handler → result/reject. `NodeagentTlvHandler` becomes a thin frame dispatcher (core frames → core, protocol frames → local scheduler by type-id). The execution path is extracted into a core-owned `RunTask` service (parse → handler lookup → admission → run → result/reject). Nodeagent schedulers additionally implement `event::CommandHandler` for node-internal events; the role is defined now, first consumer lands in a later phase. `Schedule` facet reserved for child tasks.
- **Advertisement derives from schedulers.** `NodeCapabilities.scheduling_protocols` becomes the union over the node's local schedulers' `RequiredProtocol()`, replacing the hardcoded `"push"` (`capabilities.cc:97`).
- **Config schema** changes to match: `GatewayConfig.schedulers` (repeated, type-matched), `NodeAgentConfig.scheduler` (single ExtensionConfig).

## Capabilities

### New Capabilities
- `node-local-scheduler`: nodeagent-side local scheduler extension category — the unified `Scheduler` contract applied to the node, the `NodeSchedulerFactory` + `NodeagentFactoryContext` pair, the initial push local scheduler, the thin frame dispatcher, the `RunTask` service, and the `event::CommandHandler` role for node-internal events.

### Modified Capabilities
- `pluggable-scheduler`: `Scheduler::Choose` → unified `Schedule` contract; `GatewaySchedulerFactory` + `GatewayFactoryContext`; per-type scheduler routing with a default fallback; `GatewayHttpHandler` calls `Schedule`; inbound node frames reach the scheduler via `HandleFrame`; round_robin/capability_aware reimplement as `Schedule`.
- `gateway-config`: single `scheduler` → `repeated schedulers` with task-type matching and an optional default scheduler.
- `nodeagent-config`: gains a `scheduler` ExtensionConfig naming the node's local scheduler.
- `node-advertisement`: `scheduling_protocols` built as the union over the node's local schedulers' `RequiredProtocol()` instead of the hardcoded `"push"`.
- `nodeagent-task-handlers`: `TaskHandlerFactory::Create` binds to `NodeagentFactoryContext`; the task execution path is reached through the local scheduler's `RunTask` service rather than `NodeagentTlvHandler` inline.

## Impact

- **Breaking**: `Scheduler` interface, `FactoryContext` split, scheduler factory registry split, `GatewayConfig`/`NodeAgentConfig` schemas.
- **No wire change in Phase 0**: `task-protocol` TLV framing and the `push` flow are byte-identical.
- **Code**: `src/core/gateway/*`, `src/exe/gateway/gateway.cc`, `src/core/nodeagent/*`, `src/exe/nodeagent/nodeagent.cc`, `src/core/extensions/*`, `src/extensions/schedulers/*`, `api/core/config/{gateway,nodeagent}.proto`, `api/core/node/capabilities.proto` (advertisement assembly only).
- **Tests**: `nodeagent_tlv_handler_test.cc` re-wired to the dispatcher/local-scheduler seam; scheduler tests updated to `Schedule`; new tests for the router and the push local scheduler.
- **Future phases** (design.md): capacity-released event + probe protocol, data-dependency fetcher, child-task submission with parent-owned receivers, deadlines/hardening.
