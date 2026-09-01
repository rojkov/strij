# Tasks: Extension API Layout

## 1. Consumability spine

- [x] 1.1 Add `module(name = "strij", version = "...")` declaration to `MODULE.bazel`
- [x] 1.2 Extract `RunGateway()` into a `gateway_framework` `cc_library` in `src/exe/gateway/` (keep `main()` thin), reusing existing objects
- [x] 1.3 Extract `RunNodeagent()` into a `nodeagent_framework` `cc_library` in `src/exe/nodeagent/` (keep `main()` thin)
- [x] 1.4 Move `GatewayFactoryContextImpl` and `NodeagentFactoryContextImpl` from the binary targets into the framework libraries
- [x] 1.5 Export `strij_gateway_binary(name, extensions, ...)` and `strij_nodeagent_binary(name, extensions, ...)` `.bzl` macros (framework lib + alwayslink extension targets + abseil/protobuf boilerplate)
- [x] 1.6 Rewrite `src/exe/*/BUILD.bazel` to use the exported macros
- [x] 1.7 Verify `make build && make test` still green after the extraction

## 2. Side-first repository reorg

- [x] 2.1 Move shared core packages (`event/`, `io/`, `logging/`, `config/`, `utils/`, `common/`) into `src/common/core/` and update all `BUILD` targets and includes
- [x] 2.2 Move shared extension scaffolding (`registry.hh`, `factory_context.hh`, `scheduler.hh` + `scheduler.cc`) into `src/common/extensions/`
- [x] 2.3 Move gateway core (`node_directory`, `result_receiver_storage`, `http_result_receiver`, `gateway_http_handler`, `gateway_tlv_handler`, `exact_state_tracker`) into `src/gateway/core/`
- [x] 2.4 Move nodeagent core (`admission_controller`, `run_task_service`, `task_handler_manager`, `nodeagent_tlv_handler`, `state_reporter`, `capabilities`, `function_resolver`) into `src/nodeagent/core/`
- [x] 2.5 Move gateway extensions (`node_discovery/*`, `schedulers/round_robin`, `schedulers/capability_aware`) into `src/gateway/extensions/`
- [x] 2.6 Move nodeagent extensions (`schedulers/push`, `task_handlers/echo`, `task_handlers/piped_executable`) into `src/nodeagent/extensions/`
- [x] 2.7 Stop splitting scheduling by side: move `exe` mains into `src/gateway/exe/` and `src/nodeagent/exe/`
- [x] 2.8 Mirror `api/` by side with exact naming per D6: `api/gateway/config/gateway.proto`, `api/nodeagent/config/nodeagent.proto`, shared schemas in `api/common/{config,task,node}`, extension protos beside their side's extension packages; update proto BUILD names/deps
- [x] 2.9 Mirror `test/` into `test/common|gateway|nodeagent` and update test BUILD deps (incl. auto visibility)
- [x] 2.10 Update `AGENTS.md` architecture/layout sections to the side-first tree
- [x] 2.11 Verify `make build && make test && make clang-tidy` green after the reorg

## 3. Namespace restructure

- [x] 3.1 Rename side-owned extension interfaces/factories into `strij::gateway` / `strij::nodeagent` namespaces (e.g. `strij::gateway::SchedulerFactory`, `strij::nodeagent::SchedulerFactory`)
- [x] 3.2 Nest bundled extension impls under side category sub-namespaces (e.g. `strij::gateway::schedulers::RoundRobinSchedulerFactory`)
- [x] 3.3 Keep shared scaffolding in `strij::extensions` (`Registry`, `Scheduler`, base `FactoryContext`) and shared concerns in bare namespaces (`strij::event`, `strij::io`, `strij::logging`, `strij::config`, `strij::task`)
- [x] 3.4 Ensure no `strij::common` namespace is introduced (sweep for it)
- [x] 3.5 Verify `make build && make test && make clang-tidy` green after the rename

## 4. Interface-ization of leaked consumed types

- [x] 4.1 Drop `Logger()` from the abstract `FactoryContext` (resolving the pre-existing TODO); verify no extension needs it
- [x] 4.2 Extract `gateway::NodeDirectory` into a `PURE` abstract contract under `include/` with `NodeDirectoryImpl` in `src/gateway/core/`
- [x] 4.3 Extract `gateway::ResultReceiverStorage` into an abstract contract with `ResultReceiverStorageImpl`
- [x] 4.4 Extract `nodeagent::AdmissionController`, `RunTaskService`, and `FunctionResolver` into abstract contracts with `*Impl` classes
- [x] 4.5 Move the abstract-contract headers into `include/strij/{common,gateway,nodeagent}/` and set `//visibility:public`
- [x] 4.6 Add the `Connection` seam-narrowing TODO to `Scheduler::HandleFrame` (accepted exclusion)
- [x] 4.7 Verify `make build && make test && make clang-tidy` green after interface-ization

## 5. Documentation and enforcement

- [ ] 5.1 Write the Extension Author's Guide in `docs/`: layout map, namespace doctrine, private-extension workflow (interface → `REGISTER_FACTORY` → `alwayslink` → composition macro), the visibility/stability promise
- [ ] 5.2 Add a CI check that `include/` headers reference only `include/`, absl, protobuf, and std headers (no `src/`-relative includes)
- [ ] 5.3 Add a namespace↔directory coherence check (grep-based) to CI or clang-tidy
- [ ] 5.4 Add a smoke-test consumer (e.g. `test/consumer/`) doing `bazel_dep("strij")` + `local_path_override` + one alwayslink extension composed via the macro, run by `make test`

## 6. Final validation

- [ ] 6.1 Confirm all extension targets under `src/` are non-public (`//visibility:private` unless a test mirror) and only `include/` targets are public
- [ ] 6.2 Confirm a fresh external repo can build a custom gateway+nodeagent with a private scheduler end-to-end (design sequence diagram holds)
- [ ] 6.3 Run full `make build && make test && make test_asan && make test_tsan && make clang-tidy`