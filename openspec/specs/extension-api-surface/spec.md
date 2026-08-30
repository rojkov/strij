# extension-api-surface

## Purpose

Defines the public API contract for third-party extension authors: the side-first repository layout, the public/private boundary enforced by Bazel visibility, the abstract-contract rule, the namespace doctrine, and the composition mechanism for building custom gateway and nodeagent binaries with private alwayslink extensions.

## Requirements

### Requirement: Public and private layout

The repository SHALL organize code side-first. Level one SHALL split public API (`include/strij/`) from implementation (`src/`). Level two SHALL split by side: `common/`, `gateway/`, `nodeagent/`. Inside `src/`, a third level SHALL split `core/` (required framework logic) from `extensions/` (pluggable, alwayslink-registered extensions). This side-first split SHALL be mirrored in `api/` (protobuf schemas) and `test/`.

#### Scenario: Extension category has a side

- **WHEN** a developer looks up a gateway-only extension (e.g. `round_robin` scheduler, static node discovery)
- **THEN** it SHALL be located under `src/gateway/extensions/` (and `api/gateway/`, `test/gateway/` as appropriate)
- **AND** a nodeagent-only extension (e.g. `push` scheduler, `echo` task handler) SHALL be located under `src/nodeagent/extensions/`

#### Scenario: Shared contract stays shared

- **WHEN** a type is used by both sides (e.g. the `Scheduler` contract, the extension `Registry`, the base `FactoryContext`, the event loop)
- **THEN** it SHALL be located under `common/`

### Requirement: Public API boundary

Only targets in `include/strij/` SHALL be `//visibility:public`. Targets under `src/` SHALL default to `//visibility:private` (plus automatic package-level visibility for `test/` mirrors), so that dependency across a Bazel module boundary is only possible through the public API.

#### Scenario: External repo cannot reach internals

- **WHEN** an external repository depends on Strij as a Bazel module and references a non-public target under `@strij//src/...`
- **THEN** the build SHALL fail with a visibility error

#### Scenario: External repo can reach public API

- **WHEN** an external repository references a public target under `@strij//include/strij/...`
- **THEN** the build SHALL succeed

### Requirement: Abstract-contract rule

Any concrete type an extension author consumes through a public interface shall be usable via a pure-abstract contract, with its concrete implementation hidden behind a `*Impl` class in `src/`. Extension authors SHALL NOT implement or subclass such `*Impl` classes. Freezing a concrete type into the public surface is prohibited; freezing a pure-abstract contract is permitted.

#### Scenario: Leaked service is abstract

- **WHEN** `GatewayFactoryContext::NodeDirectory()` is called by an extension
- **THEN** it SHALL return a reference to a pure-abstract `NodeDirectory` contract declared under `include/strij/`
- **AND** the concrete implementation `NodeDirectoryImpl` SHALL be located under `src/gateway/core/`

### Requirement: Connection exclusion

`io::Connection` is an accepted exception to the abstract-contract rule: it may remain a concrete type reachable from public interfaces, because abstracting it would be invasive. A TODO SHALL track narrowing the `Scheduler::HandleFrame` seam so it does not expose `Connection&`, enabling the exclusion to be removed later.

#### Scenario: Connection stays concrete

- **WHEN** the `Scheduler::HandleFrame` interface is declared
- **THEN** it MAY accept `io::Connection&` even though `Connection` is concrete and public
- **AND** a tracking TODO for narrowing the seam SHALL exist

### Requirement: Namespace doctrine

Namespaces SHALL mirror side and concern ownership, not the physical directory tree. Shared concerns SHALL use bare concern namespaces (`strij::event`, `strij::io`, `strij::logging`, `strij::config`, `strij::task`). Shared extension scaffolding SHALL use `strij::extensions` (e.g. `Registry`, `Scheduler`, base `FactoryContext`). Side-owned public types SHALL live under `strij::gateway` or `strij::nodeagent`, optionally with a category sub-namespace for implementations (e.g. `strij::gateway::schedulers::RoundRobinSchedulerFactory`). A literal `strij::common` namespace SHALL NOT exist.

#### Scenario: Namespace reflects side

- **WHEN** a gateway-only type is declared
- **THEN** it SHALL be a member of `strij::gateway` (directly or nested)
- **AND** a nodeagent-only type SHALL be a member of `strij::nodeagent`

#### Scenario: Shared type keeps shared namespace

- **WHEN** a type is declared in the `common/` tree
- **THEN** it SHALL NOT be placed in a `strij::common` namespace
- **AND** it SHALL use its bare concern namespace (e.g. `strij::io` for a connection type)

### Requirement: Consumability spine

Strij SHALL be consumable as a Bazel module: `MODULE.bazel` SHALL declare `module(name = "strij", ...)`. The gateway and nodeagent executables SHALL be split into framework libraries (`RunGateway` / `RunNodeagent`, unit-testable, each containing its side's concrete factory-context implementation) plus thin `main()` binaries. Exported composition macros `strij_gateway_binary` / `strij_nodeagent_binary` SHALL build an executable from the framework library plus a caller-supplied list of alwayslink extension targets.

#### Scenario: Private extension composed into a binary

- **WHEN** an external repository calls `strij_gateway_binary(name = "my_gateway", extensions = ["//schedulers:my_scheduler"])`
- **THEN** the resulting executable SHALL contain the gateway framework
- **AND** the `my_scheduler` extension SHALL be linked (alwayslink) into it
- **AND** registering the extension by its configured name SHALL succeed at startup

#### Scenario: Framework is a library

- **WHEN** a project links the gateway framework library
- **THEN** it SHALL link a `cc_library`, not a `cc_binary`
- **AND** the framework logic SHALL be reachable without an executable entry point

### Requirement: Extension author workflow

A third-party extension SHALL be implementable without modifying the Strij source tree: implement the side-owned public interface in the author's own repo, register its factory with `REGISTER_FACTORY` in a `.cc` file, declare the library with `alwayslink = True`, and link it through the composition macro. Runtime selection of the extension SHALL remain config-driven via the existing `ExtensionConfig`/`typed_config` mechanism.

#### Scenario: Author writes only public headers

- **WHEN** a developer writes a private gateway scheduler in a separate repository
- **THEN** every `#include` of Strij headers required by the scheduler SHALL resolve under the `strij/` include prefix
- **AND** the author SHALL NOT need to edit any file inside the Strij repository
