# Proposal

## Why

The upcoming workflow task handler must interpret documents written in the
Open Workflow DSL (OWD) — a YAML-based workflow language whose structure is
defined by the Open Workflow Specification reference. Nothing in the repo today
can turn DSL text into a typed in-memory form; the jq evaluator
(expression-evaluation) only covers the runtime-expression half of the problem.
We need a structural layer that maps DSL text into DTOs a future interpreter
can walk.

## What Changes

- Introduce a standalone **Open Workflow DSL parser SDK** in `src/openworkflow/`
  (namespace `strij::openworkflow`), designed so it can later be externalized
  as an independent `openworkflow` library and dependency of strij.
- Add a yaml-free DTO model mirroring the DSL reference 1:1: `Document`,
  `DocumentInfo`, the 12 task bodies (`Call`, `Do`, `Emit`, `For`, `Fork`,
  `Listen`, `Raise`, `Run`, `Set`, `Switch`, `Try`, `Wait`), reusable
  components (`Use`, `Authentication`, `Error`, `Timeout`, `RetryPolicy`,
  `Catalog`, `Extension`), shared value shapes (`Duration`, `Input`, `Output`,
  `Export`, `Schema`, `Endpoint`, `ExternalResource`, `ContainerLifetime`),
  and event machinery (`EventProperties`, `EventConsumptionStrategy`,
  `EventFilter`, `Correlation`, `SubscriptionIterator`).
- Parse DSL text into the model via `yaml-cpp`: the SDK's only third-party
  dependency beyond std. yaml-cpp specializations form the internal decode
  spine; the public entry point is a single
  `Parse(text) -> std::expected<Document, ParseError>`.
- Define the `ParseError` contract: a byte `position` into the original text
  plus an already-formatted message. Exceptions never cross the public API.
- Enforce **structural** rules only: exactly-one-of discriminators (task type,
  `run` process, auth scheme, backoff branch, `schema` source, consumption
  strategy), required fields, ordered task collections, and faithful opaque
  `Value` slots. Allowed-value checks and cross-field/semantic rules
  (`read`⇒`on`, `then` resolution, references into `use.*`, expression
  evaluation) are explicitly out of scope — they belong to the future
  interpreter.

## Capabilities

### New Capabilities

- `openworkflow-dsl-parser`: structural parsing of Open Workflow DSL text into
  yaml-free DTOs (document/task/use/event model), with position-carrying
  single-error-reporting `Parse()` entry point (`std::expected`-enveloped,
  throw-free public surface).

### Modified Capabilities

None.

## Impact

- New code under `src/openworkflow/` (`value`, `types`, `task`, `use`,
  `document`, `events` model headers + `parser/` yaml-cpp glue) and mirrored
  tests under `test/openworkflow/`. No existing `src/` code changes.
- Build: new Bazel packages in the OpenWorkflow SDK; dependency list is
  `@yaml_cpp` plus std — no strij-internal libraries, protos, or abseil.
- One design constraint honored: `include/` visibility is untouched — the SDK
  lives entirely under `src/` until it is externalized.
- Behavior scope marker: this change intentionally defines structure-only
  parsing. Semantic/validation behavior is deferred to a future change that
  introduces the workflow interpreter.