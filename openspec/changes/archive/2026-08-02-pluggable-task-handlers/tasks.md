## 1. Protocol & config schema

- [x] 1.1 Add `optional bool is_final = 3 [default = true]` to `TaskResult` in `api/core/task/task.proto`
- [x] 1.2 Add `repeated ExtensionConfig task_handlers = 6` (importing `core/config/extensions.proto`) to `NodeAgentConfig` in `api/core/config/nodeagent.proto`, keeping fields 3/4/5 reserved
- [x] 1.3 Create `api/extensions/task_handlers/echo/echo_task_handler.proto` with empty `EchoTaskHandlerConfig` and its BUILD target

## 2. Task handler extension interfaces

- [x] 2.1 Create `src/extensions/task_handlers/task_handlers.hh` with `ResultSender` (pure `Send(TaskResult)`), `TaskHandler` (pure `HandleTask(const Task&, ResultSender&)`), and `TaskHandlerFactory` (pure `Name()`, `CreateEmptyConfigProto()`, `Create(config, FactoryContext&) -> unique_ptr<TaskHandler>`) following the `NodeDiscovery`/`NodeDiscoveryFactory` pattern
- [x] 2.2 Add `task_handlers_interface` BUILD target in `src/extensions/task_handlers/BUILD.bazel` depending on `//src/core/extensions:extension_registry_lib`, `//src/core/extensions:factory_context_lib`, and the task proto

## 3. Nodeagent core: sender, manager, routing

- [x] 3.1 Create `src/core/nodeagent/result_sender.hh/.cc` with `ConnectionResultSender` (serializes `TaskResult`, wraps in `SerializeTlvFrame(kResult)`, calls `conn.Write`), plus BUILD deps
- [x] 3.2 Create `src/core/nodeagent/task_handler_manager.hh/.cc` with `GetHandler`, `AddHandler`, `RemoveHandler`, `empty()`, and `BuildTaskHandlerManager(configs, FactoryContext&) -> absl::StatusOr<std::shared_ptr<TaskHandlerManager>>` (registry lookup + `Any` unpack + `Create`; empty list logs warning; unknown name returns error), plus BUILD deps
- [x] 3.3 Modify `NodeagentTlvHandler` to take `std::shared_ptr<TaskHandlerManager>` in its constructor; `HandleFrame` keeps frame-type gate and `Task` parsing (malformed → log + drop), then routes `task.type()` through `GetHandler`; null → log warning + drop; else build `ConnectionResultSender(conn)` and call `HandleTask(task, sender)`
- [x] 3.4 Update `nodeagent_tlv_handler_lib` BUILD deps for the manager, sender, and task handler interface

## 4. EchoTaskHandler extension

- [x] 4.1 Create `src/extensions/task_handlers/echo/echo_task_handler.hh/.cc` with `EchoTaskHandler` (delivers `TaskResult{id, body, is_final=true}` via the sender) and `EchoTaskHandlerFactory` (`Name() == "echo"`, empty config proto, `Create` returning `EchoTaskHandler`)
- [x] 4.2 Add `echo_task_handler_lib` BUILD target (`alwayslink = True`) depending on the echo config proto and `task_handlers_interface`; register via `REGISTER_FACTORY_FULLY_QUALIFIED`

## 5. FactoryContext consolidation

- [x] 5.1 Rename `GatewayFactoryContext` to `FactoryContextImpl` in `src/core/extensions/factory_context.{hh,cc}` (same `Dispatcher()`/`Logger()` over `DispatcherSharedPtr`)
- [x] 5.2 Update `gateway.cc` call site to `FactoryContextImpl`

## 6. Nodeagent wiring & example config

- [x] 6.1 In `nodeagent.cc`: build the `FactoryContextImpl` and `TaskHandlerManager` from `config.task_handlers()` before the `--validate_only` short-circuit; on error log and exit 1; pass the manager into the TcpListener connection factory (constructing `NodeagentTlvHandler(manager)` per connection)
- [x] 6.2 Update `config/examples/nodeagent.yaml` (and `config/nodeagent.yaml` if applicable) with the `task_handlers` echo section; update any config docs referencing nodeagent settings

## 7. Tests

- [x] 7.1 Add `MockFactoryContext`, `MockTaskHandler`, `MockTaskHandlerFactory`, and `MockResultSender` to a test mocks lib (e.g. `test/mocks/extensions/`)
- [x] 7.2 Add `echo_task_handler_test` verifying `EchoTaskHandler::HandleTask` delivers a `TaskResult` with matching id/body and `is_final == true`
- [x] 7.3 Add `task_handler_manager_test` for `GetHandler` (found/nullptr), `AddHandler`/`RemoveHandler`, empty-list warning path, and unknown-name error path in `BuildTaskHandlerManager`
- [x] 7.4 Update `nodeagent_tlv_handler_test` to construct the handler with a manager; keep the echo round-trip and malformed-drop tests, add an unknown-task-type drop test
- [x] 7.5 Add `TaskResult.is_final` round-trip coverage (absent ⇒ true) in the task-protocol test

## 8. Docs & verification

- [x] 8.1 Update AGENTS.md references from `GatewayFactoryContext` to `FactoryContextImpl`, and add the task handler extension category to the Extensions/Node agent sections
- [x] 8.2 Run `make build` and `make test`; run `make clang-tidy` on changed files
