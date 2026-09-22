# Tasks

## 1. Public API surface

- [x] 1.1 Move `TaskHandler`, `ResultSender`, `TaskHandlerFactory` to `include/strij/nodeagent/task_handlers.hh`; add an `include/strij/nodeagent` BUILD target (`task_handlers_interface`, public) and update `src/nodeagent/extensions/task_handlers/BUILD.bazel`; verify `make build` resolves all `task_handlers.hh` includes.
- [x] 1.2 Move `ChildTaskForwarder` to `include/strij/nodeagent/child_task_forwarder.hh` and add a public BUILD target; verify `make check_includes` passes.
- [x] 1.3 Define `TaskHandlerDeps` in `include/strij/nodeagent/task_handlers.hh` (dispatcher, function resolver, child-task submitter); verify it compiles under `make build`.
- [x] 1.4 Define `NodeSchedulerDeps` in `include/strij/extensions/scheduler.hh` (dispatcher, run-task service, admission, child-task forwarder, data-dependency fetcher router); verify it compiles under `make build`.
- [x] 1.5 Define `DataDependencyFetcherDeps` in `include/strij/extensions/data_dependency_fetcher.hh` (dispatcher, object cache); verify it compiles under `make build`.

## 2. Factory interfaces and implementations

- [x] 2.1 Change `TaskHandlerFactory::Create` to take `const TaskHandlerDeps&`; update `EchoTaskHandlerFactory`, `PipedExecutableTaskHandlerFactory`, `WorkflowTaskHandlerFactory`, and the consumer smoke-test factory; verify `make build` compiles all task handler extensions.
- [x] 2.2 Change `NodeSchedulerFactory::Create` to take `const NodeSchedulerDeps&`; update `PushLocalSchedulerFactory`, `DefaultLocalSchedulerFactory`, `ProbeLocalSchedulerFactory`, and the consumer node scheduler; verify `make build` compiles all node schedulers.
- [x] 2.3 Change `DataDependencyFetcherFactory::Create` to take `const DataDependencyFetcherDeps&`; verify `make build` compiles the fetcher interface and mocks.

## 3. Core builders and loaders

- [x] 3.1 Split `BuildTaskHandlerManager` into "own an empty manager" plus a `TaskHandlerManager::LoadTaskHandlers(configs, deps) -> absl::Status` method; verify `task_handler_manager_test` (updated in 6.2) covers empty-list warning and unknown-name failure.
- [x] 3.2 Change `BuildDataDependencyFetchers` to take `const DataDependencyFetcherDeps&`; verify `data_dependency_fetcher_loader_test` passes.
- [x] 3.3 Change `BuildNodeagentSchedulerRouter` to take `const NodeSchedulerDeps&` and thread the deps through `CreateNodeScheduler`; verify `nodeagent_scheduler_router_test` passes.
- [x] 3.4 Add a debug assertion (or equivalent guard) that `TaskHandlerManager` is non-empty on the first `RunTask`, documenting the empty-then-populated window; verify it does not fire in existing `run_task_service` tests.

## 4. Framework wiring

- [x] 4.1 Rewrite the `RunNodeagent` service construction to the design's order: empty manager → `RunTaskServiceImpl` → fetchers → router → schedulers → router → handlers, with no `Set*()` calls; verify `make build` succeeds.
- [x] 4.2 Remove `NodeagentFactoryContext` from `include/strij/extensions/factory_context.hh` (keep `FactoryContext`/`GatewayFactoryContext`) and delete `src/nodeagent/exe/nodeagent_factory_context.{hh,cc}` plus their BUILD entries; verify `make build` and `make check_includes` pass.

## 5. Tests and mocks

- [x] 5.1 Delete `MockNodeagentFactoryContext`; update `MockTaskHandlerFactory::Create` and `MockDataDependencyFetcherFactory::Create` to the new Deps signatures; verify `make build` compiles the mocks.
- [x] 5.2 Add a test-only helper/fixture that assembles default `TaskHandlerDeps`, `NodeSchedulerDeps`, and `DataDependencyFetcherDeps`; verify the nodeagent loader tests build against it.
- [x] 5.3 Add a `StubChildTaskForwarder` test double (configurable `absl::Status`); verify it is usable from the scheduler-router tests.
- [x] 5.4 Update all nodeagent loader call sites (`task_handler_manager_test`, `nodeagent_scheduler_router_test`, `data_dependency_fetcher_loader_test`, `piped_executable_task_handler_test`, `workflow_task_handler_test`, `default_local_scheduler_test`, `probe_local_scheduler_test`) to pass Deps; verify `make test` passes.
- [x] 5.5 Add a regression test that builds the handler manager through the real factory path with the router present and asserts the workflow handler receives a valid submitter; verify it fails against the pre-change ordering and passes after.

## 6. Consumer, docs, and checks

- [x] 6.1 Update `test/consumer` scheduler factories to the new `Create` signatures; verify `make test_consumer` passes.
- [x] 6.2 Add a short migration note to the extension author guide covering the Deps bundles; verify the guide names all three bundles and the moved headers.
- [x] 6.3 Run `make check` (include purity + namespace coherence) and confirm both pass.
- [x] 6.4 Run `make test` and confirm the full suite passes with no `NodeagentFactoryContext` references remaining (grep is empty).
