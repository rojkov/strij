# Spec Delta

## Purpose

Structural parsing of Open Workflow DSL documents into a yaml-free typed model, exposing the parsed structure through a single positioned-error-reporting entry point for downstream interpreter consumption.

## ADDED Requirements

### Requirement: DSL text parses into a structured document

The system SHALL parse a complete Open Workflow DSL document (per the reference structure, DSL 1.0.3) from its text form into a structured document object. The result SHALL expose the document metadata (dsl, namespace, name, version required; title, summary, tags, metadata optional), the ordered list of top-level tasks to perform, and the optional input, use, timeout, output, schedule, and evaluate sections.

#### Scenario: Minimal document parses

- **WHEN** input text is a workflow defining only `document` and a `do` with a single task
- **THEN** parsing returns a document whose metadata matches the input and whose top-level task list contains exactly that task

#### Scenario: Example with all optional sections parses

- **WHEN** input text defines `input`, `use`, `timeout`, `output`, `schedule`, and `evaluate` alongside document metadata and tasks
- **THEN** parsing returns a document exposing every section's modeled content

### Requirement: Task body is a discriminated one-of

A task SHALL consist of optional common properties (if, input, output, export, timeout, then, metadata) plus a body discriminated by exactly one of the twelve task kinds: call, do, emit, for, fork, listen, raise, run, set, switch, try, wait. Parsing SHALL fail with a positioned error when a task declares zero or more than one of these kind keys.

#### Scenario: Single task kind parses

- **WHEN** a task declares exactly one of the twelve kind keys
- **THEN** the parsed task exposes that kind's modeled body alongside its common properties

#### Scenario: Multiple task kinds on one task fail

- **WHEN** a task declares two or more kind keys (e.g. both `call` and `wait`)
- **THEN** parsing fails with a positioned error naming the violation and the competing keys

#### Scenario: No task kind fails

- **WHEN** a task declares none of the kind keys
- **THEN** parsing fails with a positioned error stating the required kind keys

### Requirement: Task collections are ordered and named

Task collections (the workflow's top-level `do`, `do`/`for`/`try` bodies, `fork` branches, `catch` do tasks, and extension before/after task lists) SHALL parse into ordered sequences of named tasks. The reference declares these collections as `map[string, task]`, so both a genuine map form and the sequence-of-single-key-maps written form used by the reference's examples SHALL be accepted, and declaration order SHALL be preserved in the result.

#### Scenario: Sequence form preserves order

- **WHEN** a collection is written as `[ {first: …}, {second: …}, {third: …} ]`
- **THEN** the parsed collection contains the tasks in exactly that order with their declared names

#### Scenario: Map form is accepted

- **WHEN** a collection is written as a mapping of name to task
- **THEN** it parses into the same named-task representation as the sequence form

### Requirement: Reusable components parse

The `use` section SHALL parse its eight component categories — authentications, errors, timeouts, retries, catalogs, extensions, secrets, functions — each into its modeled shape. Named references to these components (timeout/error/retry/authentication reference fields) SHALL be preserved as either the referencing name string or the inline object, without resolving them.

#### Scenario: All use categories parse

- **WHEN** input text defines components in all eight categories
- **THEN** the parsed document exposes each category with its declared named entries

#### Scenario: Reference fields stay unresolved

- **WHEN** a task `timeout` is written as a string naming a `use.timeouts` entry, and another is written inline
- **THEN** both parses succeed and the string form is preserved verbatim rather than resolved to a component

### Requirement: Expressions and string-or-object unions are preserved raw

Runtime-expression fields and uri-template fields SHALL be preserved verbatim as plain strings, with no requirement that expressions be wrapped in `${ }` and no interpretation of either. Fields typed as string-or-object — durations, timeouts, error references, retry references, set data, input `from`, output/export `as` — SHALL preserve which form the author used.

#### Scenario: Bare and wrapped expressions both parse

- **WHEN** a `switch` case `when` is `'${ .a > 1 }'` and another is `.b == "x"`
- **THEN** both parse successfully with their text preserved exactly

#### Scenario: Duration accepts both forms

- **WHEN** a `wait` value is written as an ISO-8601 string in one task and as a units map (`{seconds: 30}`) in another
- **THEN** each parsed task exposes the form the author used

### Requirement: Opaque value slots preserve JSON fidelity

Opaque value slots — `metadata`, `call` `with` arguments, set-data objects, environment/port/volume mappings, argument lists, process inputs, and event payloads — SHALL be captured as values that preserve scalar kinds (boolean, integer, floating point, string) and nested structure, independent of the input text format.

#### Scenario: Mixed scalar kinds round-trip

- **WHEN** an opaque slot contains booleans, integers, a floating-point number, and strings at varying depths
- **THEN** the parsed value preserves each scalar's kind and the nesting

### Requirement: Inner one-of discriminators are enforced

Parsing SHALL enforce exactly-one-of, with a positioned error otherwise, for: the `run` process kinds (container, shell, script, workflow); an authentication's schemes (basic, bearer, digest, oauth2, oidc); a schema's source (inline document or resource); the event-consumption strategy's wait mode (all, any, one); and the retry backoff branches (constant, exponential, linear).

#### Scenario: Valid run process parses

- **WHEN** a `run` task declares exactly one of the four process kinds
- **THEN** the parsed `run` body exposes that process kind's fields

#### Scenario: Multiple authentication schemes fail

- **WHEN** an authentication declares both `basic` and `bearer`
- **THEN** parsing fails with a positioned error stating the schemes are mutually exclusive

### Requirement: Parse reports a single positioned error without throwing

Parsing SHALL succeed with the structured document or fail with exactly one error, never throwing. A reported error SHALL carry the byte position in the input text where the failure was detected and a human-readable message naming the failing construct.

#### Scenario: Syntax error carries position

- **WHEN** input text is not valid YAML
- **THEN** parsing returns an error whose position points into the input text and whose message describes the syntax failure

#### Scenario: Structural error carries position

- **WHEN** input text is valid YAML but violates a structural rule (e.g. a missing required `document` key)
- **THEN** parsing returns an error positioned at the offending construct with a message naming the violation

#### Scenario: Successful parse yields only the document

- **WHEN** input text parses successfully
- **THEN** the caller receives the document and no error

#### Scenario: Unknown key on a typed construct fails

- **WHEN** a typed construct (document, task, component, or sub-object) declares a key the DSL does not define for it
- **THEN** parsing fails with a positioned error naming the unexpected key

### Requirement: Semantic rules are deferred to the interpreter

Parsing SHALL NOT enforce allowed-value enumerations or cross-field semantic rules. Structurally valid input that is semantically questionable SHALL parse successfully: unknown enum strings, `read` set without `on`, unresolved flow directives (`then` targets that do not exist), references into `use` that do not exist, an unresolved `until` condition, and malformed runtime expressions.

#### Scenario: Unknown enum value parses

- **WHEN** a `run` task sets `return: banana`
- **THEN** parsing succeeds with the value preserved verbatim

#### Scenario: Cross-field rule violation parses

- **WHEN** a workflow `schedule` sets `read` without `on`
- **THEN** parsing succeeds, preserving both fields