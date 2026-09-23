# Extension Author's Guide

Strij is a distributed runtime for serverless functions. Third-party developers
build **private extensions** (schedulers, node discovery, task handlers) in
their own repositories and compose them into a custom gateway or node agent
binary. Strij is consumed like a library: you depend on it via Bazel, implement
an interface, register a factory, and link it into a binary with an exported
composition macro. There is no dynamic loading (`.so` plugins); everything is
statically `alwayslink`'d into one executable.

This guide covers the layout, the namespace doctrine, the private-extension
workflow, and the visibility/stability promise.

## Layout map

Strij is **side-first**: `common` / `gateway` / `nodeagent`. Each side has a
complete story across the source tree. Mirrored directories:

| | Public API | Private impl | Config protos | Tests |
|---|---|---|---|---|
| Shared | `include/strij/common/` | `src/common/{core,extensions,loaders}/` | `api/common/` | `test/common/` |
| Gateway | `include/strij/gateway/` | `src/gateway/{core,extensions,exe}/` | `api/gateway/` | `test/gateway/` |
| Nodeagent | `include/strij/nodeagent/` | `src/nodeagent/{core,extensions,exe}/` | `api/nodeagent/` | `test/nodeagent/` |

- **`include/strij/`** is the *only* surface external repositories may depend
  on. Each public header is a `//visibility:public` library (e.g.
  `//include/strij/gateway:node_directory_interface`).
- **`src/`** is private. Contract implementations live here as `*Impl` classes
  behind a pure-abstract interface.
- **`api/`** holds the protobuf schemas (config, task, node). Extension config
  schemas sit beside their extension package (e.g.
  `api/gateway/extensions/schedulers/round_robin/`).

See AGENTS.md for the full architecture.

## Namespace doctrine

Namespaces express *ownership* and dot exactly as the directory mind-map:

- Shared concerns use bare concern namespaces: `strij::event`, `strij::io`,
  `strij::logging`, `strij::config`, `strij::task`, `strij::loaders`
  (config→instance glue shared by both sides, e.g.
  `strij::loaders::CreateEvaluator`).
- Shared cross-cutting *plugin scaffolding* lives in `strij::extensions`
  (`Registry`, `Scheduler`, base `FactoryContext`). The `::extensions` marker
  is a namespace-level signal that "this is a plugin point".
- Side-owned types live in `strij::gateway` / `strij::nodeagent`, with bundled
  implementation factories nested under a category sub-namespace, e.g.
  `strij::gateway::schedulers::RoundRobinSchedulerFactory`,
  `strij::nodeagent::task_handlers::EchoTaskHandler`.
- There is **no** literal `strij::common` namespace.

The include *path* is decoupled from the filesystem via `include_prefix`, so
namespace↔path matching is a convention. A coherence check (and the repository
review checklist) keeps the two in sync — see `make check_namespaces` below.

## The abstract-contract rule

Any type that leaks into the public surface (because an extension must
implement/extend/consume it) is a **pure-abstract contract** declared in
`include/strij/`, whose concrete **`*Impl`** lives in `src/`. Consumed services
exposed on the factory contexts are all contracts:

| Context accessor | Contract (`include/strij/`) | Impl (`src/`) |
|---|---|---|
| `FactoryContext::Dispatcher()` (gateway base) | `strij/event/dispatcher.hh` (`event::Dispatcher`) | `src/common/core/event/dispatcher_impl.*` |
| `GatewayFactoryContext::NodeDirectory()` | `strij/gateway/node_directory.hh` | `src/gateway/core/node_directory.*` |
| `GatewayFactoryContext::ResultReceiverStorage()` | `strij/gateway/result_receiver_storage.hh` | `src/gateway/core/result_receiver_storage.*` |
| `TaskHandlerDeps::dispatcher_` | `strij/event/dispatcher.hh` | `src/common/core/event/dispatcher_impl.*` |
| `TaskHandlerDeps::function_resolver_` | `strij/nodeagent/function_resolver.hh` | `src/nodeagent/core/function_resolver.*` |
| `TaskHandlerDeps::child_task_submitter_` | `strij/nodeagent/child_task_submitter.hh` | `src/nodeagent/core/nodeagent_scheduler_router.*` |
| `NodeSchedulerDeps::run_task_service_` | `strij/nodeagent/run_task_service.hh` | `src/nodeagent/core/run_task_service.*` |
| `NodeSchedulerDeps::admission_` | `strij/nodeagent/admission_controller.hh` | `src/nodeagent/core/admission_controller.*` |
| `NodeSchedulerDeps::child_task_forwarder_` | `strij/nodeagent/child_task_forwarder.hh` | `src/nodeagent/core/gateway_client.*` |
| `NodeSchedulerDeps::data_dependency_fetcher_router_` | `src/nodeagent/core/data_dependency_fetcher_router.hh` (concrete) | `src/nodeagent/core/data_dependency_fetcher_router.*` |
| `DataDependencyFetcherDeps::object_cache_` | `strij/nodeagent/object_cache.hh` | `src/nodeagent/core/object_cache.*` |

**Nodeagent factory dependency bundles.** Nodeagent factories no longer receive
a shared `NodeagentFactoryContext`. Each extension category receives a small
reference bundle, passed by const reference to `Create`:

- `TaskHandlerDeps` — `strij/nodeagent/task_handlers.hh` (beside
  `TaskHandlerFactory`): `dispatcher_`, `function_resolver_`,
  `child_task_submitter_`.
- `NodeSchedulerDeps` — `strij/extensions/scheduler.hh` (beside
  `NodeSchedulerFactory`): `dispatcher_`, `run_task_service_`, `admission_`,
  `child_task_forwarder_`, `data_dependency_fetcher_router_`.
- `DataDependencyFetcherDeps` — `strij/extensions/data_dependency_fetcher.hh`
  (beside `DataDependencyFetcherFactory`): `dispatcher_`, `object_cache_`.

Reference members cannot be default-initialized, so an unset dependency is a
compile error, and the constructor order guarantees `child_task_submitter_` is a
live submitter whenever a task-handler factory runs. The gateway side keeps
`FactoryContext`/`GatewayFactoryContext`.

`io::Connection` is the one deliberate exclusion: it stays concrete on the
public surface (abstracting a leaf shell that owns an fd is invasive). The
`Scheduler::HandleFrame` seam is tracked by a TODO to narrow it so the
exclusion can later be removed.

## Private-extension workflow

The model: **interface → factory → `alwayslink` → composition macro**. Runtime
selection stays config-driven; your extension participates exactly like the
bundled ones, with zero Strij edits.

### 1. Consume the module

In your repo's `MODULE.bazel`:

```python
bazel_dep(name = "strij", version = "0.1.0")
local_path_override(
    module_name = "strij",
    path = "/absolute/path/to/strij",
)
```

(`git_override` works the same way for a published tag/commit.)

Two consumer-side requirements to be aware of:

- **C++23:** all Strij code and its public headers require `-std=c++23`; set it in
  your own `.bazelrc` (`build --cxxopt=-std=c++23 --host_cxxopt=-std=c++23`).
- **The vendored C deps** (`liburing`, `llhttp`, `yaml_cpp`) are brought in by
  repo rules declared in Strij's *root* `MODULE.bazel`. Because bzlmod only runs
  repo rules and `overrides` from the root module, a consumer must redeclare
  those `http_archive`s (and the googletest/protobuf patch overrides) in its own
  `MODULE.bazel`. `test/consumer/MODULE.bazel` is the canonical working copy;
  moving these into a module extension so consumers don't have to mirror them is
  tracked as follow-up in the extension-api-layout change.
- **`jq` (libjq 1.8.2, `//:libjq`)** is also a root-module `http_archive`, but is
  *not* part of the framework graph today: only the `src/`-internal jq
  evaluator (`//src/common/extensions/evaluators/jq:jq_evaluator_lib`) links it,
  so consumers do **not** need to redeclare `jq` unless they pull that target in
  (see the jq-expression-evaluator change). If the framework ever links
  `//:libjq`, add `jq` to the redeclaration set above.

### 2. Implement an interface

Pick the side interface your extension belongs to (scheduler, node discovery,
task handler), and depend on its public `include/` targets. Example: a gateway
scheduler registering with `strij::gateway::GatewaySchedulerFactory`.

Your factory config schema is a protobuf you own, declared beside the extension
and unpacked from the `typed_config` `@type` field.

Your extension lives in your own namespace (e.g. `acme::schedulers`), so
refer to Strij types *fully qualified* — `strij::extensions::Scheduler`,
`strij::gateway::GatewaySchedulerFactory`, `strij::task::Task` — not by the
bare `extensions::`/`gateway::` aliases the bundled code uses inside
`strij::*` namespaces.

### 3. Register the factory

Put the `REGISTER_FACTORY`/`REGISTER_FACTORY_FULLY_QUALIFIED` macro at
namespace scope in a `.cc`. For namespace-qualified factories use the
fully-qualified form with a unique `RegistrarName`:

```cpp
REGISTER_FACTORY_FULLY_QUALIFIED(
    strij::gateway::schedulers::MySchedulerFactory,
    strij::gateway::GatewaySchedulerFactory,
    my_scheduler_registrar)
```

### 4. Declare it as an always-link extension

```python
load("@strij//bazel:build_system.bzl", "strij_cc_library")

strij_cc_library(
    name = "my_scheduler_lib",
    srcs = ["my_scheduler.cc"],
    hdrs = ["my_scheduler.hh"],
    deps = [
        "@strij//include/strij/common:pure_lib",
        "@strij//include/strij/extensions:scheduler_interface",  # Scheduler contract
        "@strij//include/strij/extensions:extension_registry_lib",  # REGISTER_FACTORY_*
        # ... your generated proto, etc.
    ],
    visibility = ["//visibility:public"],
    alwayslink = True,
)
```

`alwayslink = True` is what makes the static-init registration survive into the
final binary even though nothing references the symbols directly.

### 5. Compose a binary with the exported macro

```python
load("@strij//bazel:strij_binaries.bzl", "strij_gateway_binary")

strij_gateway_binary(
    name = "my_gateway",
    extensions = ["//:my_scheduler_lib"],
)
```

The macro expands to a `cc_binary` whose deps are the gateway framework (with
`main()` and `RunGateway()`) plus your always-link extensions plus the
abseil/protobuf boilerplate. `strij_nodeagent_binary` is the node-agent
equivalent. After the binary is built, select your extension by name in the YAML
config (`schedulers:` / `node_discovery:` / `task_handlers:` sections); the
loader looks up the name in the corresponding `Registry<FactoryInterface>`.

## Visible contracts (implement these)

| Interface | Side | Factory interface used for registration |
|---|---|---|
| `strij::extensions::Scheduler` | both | `strij::gateway::GatewaySchedulerFactory` / `strij::nodeagent::NodeSchedulerFactory` |
| node discovery | gateway | `strij::gateway::NodeDiscoveryFactory` |
| task handler | nodeagent | `strij::nodeagent::TaskHandlerFactory` |

See the bundled extensions under
`src/gateway/extensions/{schedulers,node_discovery}` and
`src/nodeagent/extensions/{schedulers,task_handlers}` as reference
implementations.

## Visibility & stability promise

- **Only** `include/strij/**` targets are public. Targets under `src/` are
  private (`//visibility:private` unless a test mirror); depend only on public
  `include/` contracts. This is enforced by the include-purity check and the
  `make check` targets below.
- `include/` headers reference only `include/`, `absl`, `protobuf`, and `std`
  headers — never `src/`-relative includes.
- The intent for the future is a 1.0 API stability promise, but **this is not a
  1.0 promise yet**: the boundary mechanics are what is frozen here. Contracts
  marked `PURE` are cheap to evolve; concrete types are not.

## Repository-check targets

Run these to spot-check that the layout doctrine still holds:

```
make check_includes     # include/ may only reference include/, absl, protobuf, std
make check_namespaces   # namespace ↔ directory coherence
```

Both are grep-based and fast; they are part of the CI block.
