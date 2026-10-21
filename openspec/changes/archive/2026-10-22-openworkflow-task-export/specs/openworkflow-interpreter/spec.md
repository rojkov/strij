# Spec Delta

## MODIFIED Requirements

### Requirement: The data chain carries one value between steps and is the expression subject

The system SHALL maintain a single data value that flows from step to step, which SHALL be either a JSON value or an opaque byte value. A step's raw task input SHALL be that data value. Expressions SHALL receive the step's transformed task input as their subject: the JSON-faithful result of the task's `input.from` when declared, otherwise the raw task input.

The system SHALL seed that data value from the workflow's invocation input, which is the task body. An empty body SHALL seed JSON `null`. A non-empty body SHALL seed the data value from its bytes: JSON when they parse as JSON, opaque otherwise. If the document declares `input.from`, the system SHALL evaluate it with the invocation input as its subject and use its JSON-faithful result as the seed instead; a string result SHALL be a JSON string rather than opaque bytes. The raw task input at a step SHALL be the value produced by the step before it.

The DSL's `$context` SHALL NOT be confused with this subject. `$context` is a separate workflow-scope variable, seeded as an empty JSON object and replaced only by a task's `export.as` in this capability; a `set` task is not executed. It SHALL persist across steps and be available to runtime expressions throughout the run. It SHALL NOT observe the data chain. Document `input.from` runs before any export and therefore observes the empty seed.

#### Scenario: Data chain seeds the expression subject

- **WHEN** a workflow is invoked with a JSON object as its body and the document declares no `input.from`
- **THEN** the data value is that object and the first step's expression subject is that same object

#### Scenario: Opaque data yields a non-JSON subject

- **WHEN** a workflow is invoked with a non-empty body that is not JSON and the document declares no `input.from`
- **THEN** the data value is that non-JSON value and an expression requiring a JSON subject fails rather than reading an empty value

#### Scenario: Document input transforms the invocation body

- **WHEN** a workflow is invoked with body `{"input":5}` and the document declares `input.from: '${ .input }'`
- **THEN** the data value is `5` and the first step's expression subject is `5`

#### Scenario: Document input preserves a JSON string

- **WHEN** a workflow is invoked with body `{"name":"world"}` and the document declares `input.from: '${ .name }'`
- **THEN** the data value is the JSON string `"world"` and the first step's expression subject is that JSON string

#### Scenario: Empty body seeds JSON null

- **WHEN** a workflow is invoked with an empty body and the document declares no `input.from`
- **THEN** the data value is JSON `null`

#### Scenario: Empty body is the subject for document input

- **WHEN** a workflow is invoked with an empty body and the document declares an `input.from` expression
- **THEN** that expression is evaluated with JSON `null` as its subject

#### Scenario: Child result becomes the next subject

- **WHEN** a step completes with a raw result and declares no `output.as`
- **THEN** that result is the data value, and the following step's raw task input is that value

#### Scenario: JSONness is re-evaluated at every step boundary

- **WHEN** a step produced a JSON data value and the following step's raw output is not JSON
- **THEN** the data value for the following step is recorded as non-JSON, and a subsequent expression requiring a JSON subject fails

#### Scenario: `$context` is not the data chain

- **WHEN** a step's expression reads `$context`
- **THEN** it observes the workflow context, seeded as an empty object, and never observes the data chain's current value

### Requirement: Validation that does not depend on data completes before any child is submitted

The system SHALL perform a validation pass over the document before executing its first step. That pass SHALL cover the facts derivable from the document alone: the call target of each call, the key set of each call's arguments, the value kind of each declared requirement, and the syntax and whole-value wrapping of every runtime expression, including each task's `if`, `input.from`, `output.as`, and `export.as` and the document's `input.from` and `output.as`. The pass SHALL cover every task in every nested sequence, including the branch tasks of every fork. A fork declared to compete with no branches SHALL be rejected in this pass, and a task that declares `export` inside a fork branch SHALL be rejected in this pass as unimplemented. A failure in this pass SHALL be reported before any child task is submitted. Failures that depend on data SHALL instead occur during execution.

#### Scenario: Malformed document fails before submitting anything

- **WHEN** a document declares an unknown call target, an unknown key in a call's arguments, a non-integer requirement, or an invalid expression
- **THEN** the workflow fails with that error and no child task has been submitted

#### Scenario: Malformed condition or output expression fails before submitting anything

- **WHEN** a task declares an `if` or `output.as` expression that is not a valid program, or the document declares an invalid `output.as`
- **THEN** the workflow fails with a malformed-document error and no child task has been submitted

#### Scenario: Malformed export expression fails before submitting anything

- **WHEN** a task declares an `export.as` expression that is not a valid program
- **THEN** the workflow fails with a malformed-document error and no child task has been submitted

#### Scenario: Data-dependent failure occurs during execution

- **WHEN** a document is structurally valid but a step's expression is given a non-JSON subject at run time
- **THEN** the failure occurs during execution rather than in the pre-execution pass

#### Scenario: Validation errors are distinguishable from run errors

- **WHEN** either kind of failure occurs
- **THEN** the reported error indicates whether the document is malformed or the run was given the wrong data

#### Scenario: Malformed task inside a fork branch fails before submitting anything

- **WHEN** a fork branch declares a task with an unknown call target or an invalid expression
- **THEN** the workflow fails with that error during the validation pass and no child task has been submitted

#### Scenario: A competing fork without branches is malformed

- **WHEN** a document declares a fork with `compete: true` and no branches
- **THEN** the workflow fails in the validation pass with a malformed-document error and no child task has been submitted

### Requirement: Expression-typed slots accept only whole-value wrapped expressions

For the expression-typed slots — a task's `if`, a task's `input.from`, a task's `output.as`, a task's `export.as`, the document's `input.from`, and the document's `output.as` — the system SHALL treat only a string wholly wrapped in `${ }` as an expression. A bare string in one of these slots SHALL be rejected as a malformed document rather than treated as literal text. When the slot uses an object form, each nested string member SHALL follow the same rule. Literal-capable value slots, such as a call's `with` values and task `metadata`, SHALL retain the literal-text rule.

#### Scenario: Bare condition is malformed

- **WHEN** a task declares `if: .enabled`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare input transformation is malformed

- **WHEN** a task declares `input.from: .payload`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare output transformation is malformed

- **WHEN** a task declares `output.as: .result`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare export transformation is malformed

- **WHEN** a task declares `export: { as: .result }`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Nested object member must be wrapped

- **WHEN** a task declares `input.from: { id: .payload.id }`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Wrapped forms are accepted

- **WHEN** a task declares `if: '${ .enabled }'`, `input.from: '${ .payload }'`, `output.as: '${ .result }'`, and `export.as: '${ { token: .token } }'`
- **THEN** the document parses, validates, and executes those slots as expressions

#### Scenario: Literal value slots are unaffected

- **WHEN** a call's `with` value is the unwrapped string `/bin/task.sh`
- **THEN** it remains literal text and is not treated as an expression

### Requirement: Document output.as transforms the workflow's final output

When a workflow completes successfully and the document declares `output.as`, the system SHALL evaluate it against the last transformed data value and use its JSON-faithful result as the final result body. The last data value SHALL be the expression subject, and `$context` SHALL be present with the current workflow context. When the document declares no `output.as`, the last data value SHALL be the final result body. A declared `output.as` over a final value that is not JSON SHALL fail the workflow during execution with a subject error.

#### Scenario: Document output shapes the final result

- **WHEN** the last step's transformed output is `{"id":7}` and the document declares `output.as: '${ { petId: .id } }'`
- **THEN** the final result body is `{"petId":7}`

#### Scenario: Document output reads the current context

- **WHEN** a prior step exported `{token:"abc"}` and the document declares `output.as: '${ { token: $context.token, result: . } }'`
- **THEN** the final result body contains the exported token

#### Scenario: No document output preserves the last value

- **WHEN** the document declares no `output.as`
- **THEN** the final result body is the last data value, including opaque bytes

#### Scenario: Document output over non-JSON fails

- **WHEN** the document declares `output.as` and the last data value is not JSON
- **THEN** the workflow fails during execution with an error stating the subject is not JSON

#### Scenario: Malformed document output fails before submitting

- **WHEN** the document declares an `output.as` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted

## ADDED Requirements

### Requirement: Task export.as updates the workflow context

When a task declares `export` with `as`, the system SHALL evaluate `export.as` against the step's transformed task output after the step's output transformation completes and before the next step begins. The transformed output SHALL be the expression subject; `$output` SHALL name that transformed output, `$input` SHALL name the step's transformed input, and `$context` SHALL name the current context. The result SHALL be a JSON object and SHALL replace the workflow context. A task with no `export`, or an `export` with no `as`, SHALL leave the context unchanged.

`export.as` SHALL run only for a step that completed successfully and was not skipped by `if`. A failed or skipped step SHALL NOT export. It SHALL apply to `call`, `do`, and `fork` steps. A declared `export.as` SHALL require both the transformed input and the transformed output to be JSON; if either is opaque, the step SHALL fail during execution with a subject error naming the step. A result that is not a JSON object SHALL fail the step during execution.

A task that declares `export` inside a fork branch SHALL be rejected during the pre-execution validation pass as unimplemented, because fork branches write concurrently and no context-combination semantics are defined. A `fork` task's own `export`, outside its branches, SHALL be supported.

#### Scenario: Export writes context for later steps

- **WHEN** a task completes with transformed output `{"token":"abc"}` and declares `export.as: '${ { token: .token } }'`
- **THEN** the following step's `$context` is `{"token":"abc"}`

#### Scenario: Export merges by expression

- **WHEN** the current context is `{"a":1}` and a task declares `export.as: '${ $context + { b: 2 } }'`
- **THEN** the context becomes `{"a":1,"b":2}`

#### Scenario: Export replaces the context

- **WHEN** the current context is `{"a":1}` and a task declares `export.as: '${ . }'`
- **THEN** the context becomes the task's transformed output, replacing the prior context

#### Scenario: Export reads input and output arguments

- **WHEN** a task declares `export.as: '${ { before: $input, after: $output } }'`
- **THEN** `$input` is the transformed task input and `$output` is the transformed task output

#### Scenario: No export leaves context unchanged

- **WHEN** a task declares no `export`, or declares `export` without `as`
- **THEN** the workflow context is unchanged

#### Scenario: Skipped task does not export

- **WHEN** a task declares `if: '${ false }'` and an `export.as` expression
- **THEN** no export runs and the context is unchanged

#### Scenario: Failed task does not export

- **WHEN** a task's child fails and the task declares `export.as`
- **THEN** no export runs and the workflow fails with the child's status

#### Scenario: Composite task exports its transformed output

- **WHEN** a `do` or `fork` task declares `output.as` and `export.as`
- **THEN** `export.as` is evaluated against the shaped output, not the raw nested or join value

#### Scenario: Non-object export result fails

- **WHEN** `export.as` evaluates to an array, scalar, or null
- **THEN** the workflow fails during execution with an error stating the exported context must be an object and naming the step

#### Scenario: Export over opaque values fails

- **WHEN** a task declares `export.as` and its transformed input or transformed output is not JSON
- **THEN** the workflow fails during execution with an error stating the value is not JSON and naming the step

#### Scenario: Export inside a fork branch is rejected

- **WHEN** a task inside a fork branch — directly or in a nested `do` — declares `export`
- **THEN** the workflow fails in the pre-execution validation pass with an unimplemented status and no child task has been submitted

#### Scenario: Fork task export is allowed

- **WHEN** the task that declares `fork` also declares `export.as`
- **THEN** the export applies to the fork's transformed output after its join resolves

#### Scenario: Malformed export fails before submitting

- **WHEN** a task declares an `export.as` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted
