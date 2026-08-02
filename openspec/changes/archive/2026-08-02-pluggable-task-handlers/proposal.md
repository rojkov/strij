## Why

`NodeagentTlvHandler` hardcodes task processing: every `Task` is echoed back verbatim as a `TaskResult`. To support real task types (e.g., spawning processes, streaming results), task processing must become pluggable. The existing Envoy-style extension mechanism (`Registry<FactoryInterface>` + `ExtensionConfig` with `Any` typed_config) already provides the infrastructure — this change applies it to the nodeagent's task handling.

## What Changes

- **New `TaskHandler` extension category**: abstract `TaskHandler` interface and `TaskHandlerFactory` factory interface, mirroring the `NodeDiscovery`/`NodeDiscoveryFactory` pattern. Handlers receive the parsed `Task` and deliver results through a per-connection `ResultSender` (async-friendly; a sync handler simply sends immediately).
- **New `TaskHandlerManager`**: owns instantiated task handlers, keyed by task type (factory name). It is built once from `NodeAgentConfig.task_handlers` and **shared** across connections (via `shared_ptr`) so task handlers hold shared state (e.g., a process pool). It exposes the runtime reconfiguration seam (`AddHandler`/`RemoveHandler`) for a future control-plane, but no runtime updates in this change.
- **`NodeagentTlvHandler` routing**: `HandleFrame()` keeps parsing `Task` from `kTaskSubmission` frames (malformed → log + drop), then looks up the handler for `task.type()` in the manager and delegates. Unknown task type → log warning + drop. Behavior with `echo` configured is unchanged from today.
- **`EchoTaskHandler` extension**: first task handler, registered as `"echo"` in `Registry<TaskHandlerFactory>`, reproducing the current echo semantics.
- **Config**: `NodeAgentConfig` gains `repeated ExtensionConfig task_handlers`. An empty list logs a warning (the manager may be reconfigured at runtime in the future). An unknown handler name fails fast at startup (mirrors gateway's node_discovery check) and is covered by `--validate_only`. `config/examples/nodeagent.yaml` is updated.
- **`FactoryContext` consolidation**: `GatewayFactoryContext` is renamed to `FactoryContextImpl` (works for both gateway and nodeagent); a `MockFactoryContext` is added for unit tests.
- **Protocol**: `TaskResult` gains `optional bool is_final = 3` as the forward-compatible seam for streaming results. Absence means final (proto3 forbids a `default = true`, so consumers encode `!has_is_final() || is_final()` as "final"), keeping existing single-shot results completing requests. Today's gateway ignores the field.

## Capabilities

### New Capabilities
- `nodeagent-task-handlers`: TaskHandler extension category for the nodeagent — `TaskHandler`/`TaskHandlerFactory` interfaces, `TaskHandlerManager` with type-based routing, `ResultSender` result delivery, and the `EchoTaskHandler` extension.

### Modified Capabilities
- `gateway-task-bridge`: the "NodeagentTlvHandler echoes task bodies" requirement changes — `NodeagentTlvHandler` no longer echoes directly; it routes parsed `Task`s to a `TaskHandler` obtained from `TaskHandlerManager` by `task.type()`. The echo end-to-end flow requirement stays behaviorally equivalent.
- `task-protocol`: `TaskResult` schema gains the `optional bool is_final` field.
- `nodeagent-config`: `NodeAgentConfig` schema gains the `task_handlers` repeated `ExtensionConfig` field.
- `extension-registry`: the `FactoryContext` requirement changes — `GatewayFactoryContext` is replaced by a general `FactoryContextImpl` concrete implementation.

## Impact

- **Code**: `src/core/nodeagent/nodeagent_tlv_handler.{hh,cc}` (routing), new `src/extensions/task_handlers/task_handlers.hh` (interfaces) and `src/extensions/task_handlers/echo/` (EchoTaskHandler + config proto + Bazel target with `alwayslink`), new `src/core/nodeagent/task_handler_manager.{hh,cc}`, new `ResultSender` abstraction, `src/core/extensions/factory_context.{hh,cc}` rename, `src/exe/nodeagent/nodeagent.cc` (build manager from config, wire into connection factory).
- **Protobuf**: `api/core/task/task.proto` (TaskResult.is_final), `api/core/config/nodeagent.proto` (task_handlers field), new `api/extensions/task_handlers/echo/echo_task_handler.proto`.
- **Config**: `config/examples/nodeagent.yaml`, docs.
- **Tests**: `nodeagent_tlv_handler_test` updated for manager injection; new `task_handler_manager_test`, `echo_task_handler_test`, `MockFactoryContext`. Echo behavior unchanged.
- **Docs**: AGENTS.md references to `GatewayFactoryContext` updated.
