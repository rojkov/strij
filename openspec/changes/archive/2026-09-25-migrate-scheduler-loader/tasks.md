# Tasks

## 1. Relocate loader source

- [x] 1.1 Move `src/common/extensions/scheduler_loader.hh` → `src/common/loaders/scheduler_loader.hh` and `src/common/extensions/scheduler.cc` → `src/common/loaders/scheduler_loader.cc`; verify `rg --files src/common/extensions` shows only `evaluators/` remains.
- [x] 1.2 In the header, replace the `strij::gateway::CreateGatewayScheduler` / `strij::nodeagent::CreateNodeScheduler` declarations with two `strij::loaders::CreateScheduler` overloads (one taking `extensions::GatewayFactoryContext&`, one taking `const nodeagent::NodeSchedulerDeps&`); verify the header opens only namespace `strij::loaders`.
- [x] 1.3 In the `.cc`, move `createSchedulerFromExtension` into `namespace strij::loaders { namespace { ... } }` and define both overloads forwarding to it; verify the `.cc` opens only `strij::loaders` (+ anonymous) and preserves the existing error strings, `typed_config` tolerance, and null-rejection.

## 2. Build target and consumers

- [x] 2.1 Add `scheduler_loader_lib` to `src/common/loaders/BUILD.bazel` (`srcs = ["scheduler_loader.cc"]`, `hdrs = ["scheduler_loader.hh"]`, same deps/visibility) and delete `src/common/extensions/BUILD.bazel`; verify `bazel query '//src/common/loaders:*'` lists the target and `bazel query '//src/common/extensions/evaluators:*'` still resolves `evaluator_interface`.
- [x] 2.2 Update `//src/common/extensions:scheduler_loader_lib` → `//src/common/loaders:scheduler_loader_lib` and `common/extensions/scheduler_loader.hh` → `common/loaders/scheduler_loader.hh` in `src/gateway/core/scheduler_router/{BUILD.bazel,scheduler_router.cc}` and `src/nodeagent/core/{BUILD.bazel,nodeagent_scheduler_router.cc}`, qualifying calls as `loaders::CreateScheduler(...)`; verify `rg "common/extensions/scheduler_loader|extensions:scheduler_loader_lib" src` returns nothing.
- [x] 2.3 Run `make build` and verify it succeeds.

## 3. Tests

- [x] 3.1 Move `test/common/extensions/scheduler_factory_test.cc` → `test/common/loaders/scheduler_factory_test.cc` (with its `BUILD.bazel`), update the loader dep/include path, and change calls to `loaders::CreateScheduler`; verify `rg "scheduler_factory_test|scheduler_loader" test` points only at the new path.
- [x] 3.2 Run the moved test target and verify the empty-name/unknown-name rejection, typed-config, and creation assertions all pass unchanged.

## 4. Tooling

- [x] 4.1 In `tools/check_namespace_coherence.py`, remove the `src/common/extensions/scheduler.hh`, `src/common/extensions/scheduler.cc`, and `src/common/extensions/scheduler_loader.hh` allowlist entries, and repoint the cross-cutting test entry to `test/common/loaders/scheduler_factory_test.cc`; run `make check_namespaces` and verify it passes.

## 5. Docs

- [x] 5.1 Update the `AGENTS.md` layout bullet and scheduler bullet from `src/common/extensions/scheduler_loader` to `src/common/loaders/scheduler_loader` and note the `CreateScheduler` overload pair; verify `rg "scheduler_loader|CreateScheduler" AGENTS.md` shows no stale `src/common/extensions` path.

## 6. Deferred follow-up: shared `ResolveExtensionConfig` helper

- [x] 6.1 Add a `TODO` comment in `src/common/loaders/scheduler_loader.cc` (by the registry-lookup/unpack preamble), referenced from `evaluator_loader.cc`, stating: when a third loader category is introduced, extract the shared preamble into `template <typename FactoryT> ResolveExtensionConfig(const config::ExtensionConfig&, std::string_view kind) -> absl::StatusOr<std::unique_ptr<google::protobuf::Message>>` in `strij::loaders`, leaving each category's create/compile step (and its null semantics) local; verify `rg "ResolveExtensionConfig" src/common/loaders/` finds it.

## 7. Verification

- [x] 7.1 Run `make check` (include purity + namespace coherence) and verify both pass.
- [x] 7.2 Run `make test` and verify the full suite passes.
- [x] 7.3 Run `rg "CreateGatewayScheduler|CreateNodeScheduler" src test include` and verify no references remain outside the `openspec/changes/archive/` history.
