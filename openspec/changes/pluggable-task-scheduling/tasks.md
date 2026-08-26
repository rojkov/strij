## 1. Shared interfaces (foundation)

- [ ] 1.1 Redefine `extensions::Scheduler` (`src/extensions/schedulers/scheduler.hh`) as the unified interface: `Schedule(const task::Task&, ResultReceiverPtr)` (pure), `RequiredProtocol()` (pure), `HandleFrame(io::TlvFrame, io::Connection&)` (default no-op), `HandledFrameTypes()` (default empty); remove `Choose` and `TaskOffer`; document the "every task resolves to a result or error" invariant.
- [ ] 1.2 Split `FactoryContext` into a minimal base (Dispatcher, Logger) with `GatewayFactoryContext` (adds NodeDirectory, ResultReceiverStorage) and `NodeagentFactoryContext` (adds FunctionResolver, AdmissionController, RunTask service); retire `FactoryContextImpl`; add per-side implementations.
- [ ] 1.3 Introduce `GatewaySchedulerFactory` and `NodeSchedulerFactory` interfaces (both with `Name()`, `CreateEmptyConfigProto()`, `Create(config, <side>FactoryContext&)` returning `SchedulerPtr`); keep the shared `SchedulerPtr` alias.
- [ ] 1.4 Re-bind existing factory signatures: `TaskHandlerFactory::Create` and `BuildTaskHandlerManager` take `NodeagentFactoryContext&`; `NodeDiscoveryFactory::Create` takes `GatewayFactoryContext&`.

## 2. Nodeagent half (Phase 0A)

- [ ] 2.1 Add `NodeAgentConfig.scheduler` (ExtensionConfig) to `api/core/config/nodeagent.proto`.
- [ ] 2.2 Extract the execution path from `NodeagentTlvHandler::HandleFrame` into a core-owned `RunTask` service (`core/nodeagent`): parse → `GetHandler(type)` → `Admit` → `kTaskRejected` on failure → `AdmissionScope` + `AdmissionTrackingSender` + `ConnectionResultSender` → `handler->HandleTask`; expose it via `NodeagentFactoryContext`.
- [ ] 2.3 Implement `PushLocalScheduler` (`src/extensions/schedulers/push` or `nodeagent`-side extension dir) registered as `"push"` in `Registry<NodeSchedulerFactory>`: `RequiredProtocol()=="push"`, `HandledFrameTypes()=={kTaskSubmission}`, `HandleFrame` parses and calls `RunTask`, `Schedule` delivers an error, and it implements `event::CommandHandler` (no-op `ProcessCommand`).
- [ ] 2.4 Convert `NodeagentTlvHandler` into a frame dispatcher: constructed with the local scheduler, routes frames whose type is in `HandledFrameTypes()` to `scheduler->HandleFrame(frame, conn)`, drops others, and keeps `SendAdvertisement` unchanged.
- [ ] 2.5 Update `BuildNodeCapabilities` to append `RequiredProtocol()` from the configured local scheduler(s) to `scheduling_protocols` (union), replacing the hardcoded `"push"`.
- [ ] 2.6 Wire `nodeagent.cc`: build the `NodeagentFactoryContextImpl`, construct the local scheduler from config (default `"push"` when unset; fail to start on an unknown name), and pass it into the dispatcher.
- [ ] 2.7 Rewrite `nodeagent_tlv_handler_test.cc` against the dispatcher + push local scheduler seam; add tests for `RunTask` (admit/reject/drop), the push scheduler (submission behavior preserved, unknown frame dropped, `Schedule` error), dispatcher routing, advertisement union, and `NodeAgentConfig` scheduler loading.

## 3. Gateway half (Phase 0B)

- [ ] 3.1 Add `SchedulerConfig` (`ExtensionConfig extension`, `string task_type`; empty `task_type` = default) and `repeated schedulers` to `api/core/config/gateway.proto`; update the YAML loader to also normalize a legacy single `scheduler:` section into one default `SchedulerConfig`.
- [ ] 3.2 Implement `SchedulerRouter` as a composite `Scheduler`: built from `GatewayConfig.schedulers`, dispatches `Schedule` by `task.type()` to the matched scheduler, falls back to the default, and delivers an error when no match and no default exist.
- [ ] 3.3 Convert `round_robin` to `Schedule`: `Choose` becomes a private selection helper; `Schedule` submits to the chosen node; `DeliverError` when no node is available.
- [ ] 3.4 Convert `capability_aware` to `Schedule` with the same treatment (eligibility + least-loaded selection; `DeliverError` when none eligible).
- [ ] 3.5 Change `GatewayHttpHandler` to resolve requirements, register the receiver in `ResultReceiverStorage`, and call the router's `Schedule` fire-and-forget; remove node-selection and node-frame-writing from the HTTP path.
- [ ] 3.6 Wire `gateway.cc`: build `GatewayFactoryContextImpl`, construct all configured schedulers (fail to start when none or an unknown name), build the router, and hand it to `GatewayHttpHandler` and `GatewayTlvHandler`'s frame routing seam.
- [ ] 3.7 Update scheduler tests (`round_robin`, `capability_aware`) for `Schedule` semantics; add router tests (type match, default fallback, no-default error) and a `GatewayHttpHandler` fire-and-forget test.

## 4. Build and verification

- [ ] 4.1 Update BUILD.bazel targets and deps for the new interfaces, extensions, and moved code (gateway, nodeagent, extensions/schedulers).
- [ ] 4.2 Run `make build` and `make test`; fix failures; run `make clang-tidy` on changed files.
- [ ] 4.3 Update `AGENTS.md` architecture notes for the unified `Scheduler` contract, the split factory contexts, and the node-local-scheduler extension category.
