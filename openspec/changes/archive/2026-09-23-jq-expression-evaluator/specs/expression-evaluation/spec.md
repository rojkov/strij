# Spec Delta

## Purpose

Defines the shared expression-evaluation extension category used by workflow-style task handlers: an evaluator contract that compiles expression sources and runs them against JSON inputs, a config-driven factory shape for plugging in alternative implementations (jq being the reference one), and the JSON value safety contract at the boundary.

## ADDED Requirements

### Requirement: Evaluator extension category

The system SHALL provide an expression evaluator extension category shared by gateway and nodeagent consumers, with evaluator instances created through a factory registered in the extension `Registry` and resolved at runtime by the `name` in an `ExtensionConfig`. An evaluator instance SHALL compile an expression source into a reusable compiled program and run it zero or more times against differing inputs.

> Note: bytecode reuse across runs with differing variable bindings is not possible with libjq. `jq_compile_args` resolves declared variables to compile-time constants (`LOADK`) rather than runtime variable slots (`LOADV`/`frame_local_var`), so applying caller-supplied argument values requires recompiling the source. The reference `jq` evaluator therefore validates at `Compile` and recompiles per `Run`; the evaluator handle itself stays immutable and reusable.

#### Scenario: Compile then run

- **WHEN** an evaluator is created and a syntactically valid expression is compiled
- **THEN** the compiled program SHALL be runnable against an input value
- **AND** it SHALL return the expression's output value(s)

#### Scenario: Compile error surfaces as a value

- **WHEN** a syntactically invalid expression source is compiled
- **THEN** an error describing the failure SHALL be returned
- **AND** no evaluator instance or compiled program SHALL be left in an unusable state

#### Scenario: Named input variables

- **WHEN** an expression references a named variable (e.g. a workflow input or task context)
- **THEN** the evaluator SHALL bind the variable from caller-supplied named arguments at run time

### Requirement: Reference jq semantics

The system SHALL ship an evaluator registered under the name `"jq"` whose expression semantics match the reference jq implementation, including multi-output generators and jq runtime error behavior.

#### Scenario: Faithful expression results

- **WHEN** a program exercising jq constructs (filtering, mapping, length, string interpolation) is run against a JSON input
- **THEN** the output SHALL match the reference jq evaluation of the same program and input

#### Scenario: Runtime expression error

- **WHEN** a compiled program halts with a jq runtime error (e.g. a type error)
- **THEN** the evaluator SHALL return an error carrying jq's message
- **AND** the failed run SHALL NOT corrupt the compiled program for later runs

### Requirement: JSON value boundary

An evaluator SHALL accept and produce JSON values, not expression-language source text. Consumers SHALL be able to construct an input from JSON text and render an output back to JSON text, with value lifetime managed by the evaluator's owning types.

#### Scenario: JSON round-trip

- **WHEN** a consumer supplies an input as JSON text and renders the result of a successful run as JSON text
- **THEN** the rendered value SHALL represent the same data as the evaluator's output value
- **AND** the consumer SHALL NOT need to manage the value's memory manually

### Requirement: Config-driven evaluator instantiation

The system SHALL load an evaluator from an `ExtensionConfig`: resolve the factory by `name` in the evaluator `Registry`, unpack (or tolerate the absence of) its `typed_config`, and create the instance. Loader failures SHALL be reported as errors, and the `name` SHALL be the only selection lever, so a consumer may bind any registered evaluator to a given expression language without changing the consumer.

#### Scenario: Named evaluator is created

- **WHEN** a loader is given an `ExtensionConfig` whose `name` is a registered evaluator factory
- **THEN** an evaluator instance configured from the `typed_config` SHALL be returned

#### Scenario: Unregistered evaluator name

- **WHEN** a loader is given an `ExtensionConfig` whose `name` is not registered
- **THEN** a not-found error SHALL be returned

#### Scenario: Misconfigured evaluator pins a single language

- **WHEN** two different registered evaluator factories are bound, one at a time, to the same language name in two configurations
- **THEN** each configuration SHALL produce the evaluator instance named by its own `name`