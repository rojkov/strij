# Spec Delta

## MODIFIED Requirements

### Requirement: Public and private layout

The repository SHALL organize code side-first. Level one SHALL split public API (`include/strij/`) from implementation (`src/`). Level two SHALL split by side: `common/`, `gateway/`, `nodeagent/`. Inside `src/`, a third level SHALL split `core/` (required framework logic), `extensions/` (shared pluggable, alwayslink-registered extensions by category), and `loaders/` (shared config-to-instance bridge glue that turns an `ExtensionConfig` into a configured extension instance). This side-first split SHALL be mirrored in `api/` (protobuf schemas) and `test/`.

#### Scenario: Extension category has a side

- **WHEN** a developer looks up a gateway-only extension (e.g. `round_robin` scheduler, static node discovery)
- **THEN** it SHALL be located under `src/gateway/extensions/` (and `api/gateway/`, `test/gateway/` as appropriate)
- **AND** a nodeagent-only extension (e.g. `push` scheduler, `echo` task handler) SHALL be located under `src/nodeagent/extensions/`

#### Scenario: Shared contract stays shared

- **WHEN** a type is used by both sides (e.g. the `Scheduler` contract, the extension `Registry`, the base `FactoryContext`, the event loop)
- **THEN** it SHALL be located under `common/`

#### Scenario: Shared loader glue has a home

- **WHEN** a both-sides loader that turns an extension's `ExtensionConfig` into a configured instance (e.g. `CreateEvaluator`) is placed
- **THEN** it SHALL be located under `src/common/loaders/`
- **AND** it SHALL NOT be placed under `src/common/extensions/` or the side `core/` trees

### Requirement: Namespace doctrine

Namespaces SHALL mirror side and concern ownership, not the physical directory tree. Shared concerns SHALL use bare concern namespaces (`strij::event`, `strij::io`, `strij::logging`, `strij::config`, `strij::task`, `strij::loaders`). Shared extension scaffolding SHALL use `strij::extensions` (e.g. `Registry`, `Scheduler`, base `FactoryContext`, categorized shared extension implementations such as `evaluators::`). Side-owned public types SHALL live under `strij::gateway` or `strij::nodeagent`, optionally with a category sub-namespace for implementations (e.g. `strij::gateway::schedulers::RoundRobinSchedulerFactory`). A literal `strij::common` namespace SHALL NOT exist.

#### Scenario: Namespace reflects side

- **WHEN** a gateway-only type is declared
- **THEN** it SHALL be a member of `strij::gateway` (directly or nested)
- **AND** a nodeagent-only type SHALL be a member of `strij::nodeagent`

#### Scenario: Shared type keeps shared namespace

- **WHEN** a type is declared in the `common/` tree
- **THEN** it SHALL NOT be placed in a `strij::common` namespace
- **AND** it SHALL use its bare concern namespace (e.g. `strij::io` for a connection type)

#### Scenario: Loader glue has a namespace

- **WHEN** a both-sides loader is declared under `src/common/loaders/`
- **THEN** it SHALL be a member of the bare `strij::loaders` namespace
- **AND** it SHALL NOT live in `strij::extensions` unless it is itself an extension implementation