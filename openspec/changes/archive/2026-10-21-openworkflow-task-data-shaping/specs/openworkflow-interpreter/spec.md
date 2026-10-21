# Spec Delta

## MODIFIED Requirements

### Requirement: The data chain carries one value between steps and is the expression subject

The system SHALL maintain a single data value that flows from step to step, which SHALL be either a JSON value or an opaque byte value. A step's raw task input SHALL be that data value. Expressions SHALL receive the step's transformed task input as their subject: the JSON-faithful result of the task's `input.from` when declared, otherwise the raw task input.

The system SHALL seed that data value from the workflow's invocation input, which is the task body. An empty body SHALL seed JSON `null`. A non-empty body SHALL seed the data value from its bytes: JSON when they parse as JSON, opaque otherwise. If the document declares `input.from`, the system SHALL evaluate it with the invocation input as its subject and use its JSON-faithful result as the seed instead; a string result SHALL be a JSON string rather than opaque bytes. The raw task input at a step SHALL be the value produced by the step before it.

The DSL's `$context` SHALL NOT be confused with this subject. `$context` is a separate workflow-scope variable, seeded empty and written only by an `export` or `set` task, neither of which this capability executes. `$context` SHALL therefore be present and empty throughout this capability's execution. A document that reads `$context` in this capability SHALL observe an empty object, and SHALL NOT observe the data chain.

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
- **THEN** it observes an empty object, not the data chain's current value

### Requirement: Validation that does not depend on data completes before any child is submitted

The system SHALL perform a validation pass over the document before executing its first step. That pass SHALL cover the facts derivable from the document alone: the call target of each call, the key set of each call's arguments, the value kind of each declared requirement, and the syntax and whole-value wrapping of every runtime expression, including each task's `if`, `input.from`, and `output.as` and the document's `input.from` and `output.as`. The pass SHALL cover every task in every nested sequence, including the branch tasks of every fork. A fork declared to compete with no branches SHALL be rejected in this pass. A failure in this pass SHALL be reported before any child task is submitted. Failures that depend on data SHALL instead occur during execution.

#### Scenario: Malformed document fails before submitting anything

- **WHEN** a document declares an unknown call target, an unknown key in a call's arguments, a non-integer requirement, or an invalid expression
- **THEN** the workflow fails with that error and no child task has been submitted

#### Scenario: Malformed condition or output expression fails before submitting anything

- **WHEN** a task declares an `if` or `output.as` expression that is not a valid program, or the document declares an invalid `output.as`
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

## ADDED Requirements

### Requirement: Expression-typed slots accept only whole-value wrapped expressions

For the expression-typed slots — a task's `if`, a task's `input.from`, a task's `output.as`, the document's `input.from`, and the document's `output.as` — the system SHALL treat only a string wholly wrapped in `${ }` as an expression. A bare string in one of these slots SHALL be rejected as a malformed document rather than treated as literal text. When the slot uses an object form, each nested string member SHALL follow the same rule. Literal-capable value slots, such as a call's `with` values and task `metadata`, SHALL retain the literal-text rule.

#### Scenario: Bare condition is malformed

- **WHEN** a task declares `if: .enabled`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare input transformation is malformed

- **WHEN** a task declares `input.from: .payload`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare output transformation is malformed

- **WHEN** a task declares `output.as: .result`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Nested object member must be wrapped

- **WHEN** a task declares `input.from: { id: .payload.id }`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Wrapped forms are accepted

- **WHEN** a task declares `if: '${ .enabled }'`, `input.from: '${ .payload }'`, and `output.as: '${ .result }'`
- **THEN** the document parses, validates, and executes those slots as expressions

#### Scenario: Literal value slots are unaffected

- **WHEN** a call's `with` value is the unwrapped string `/bin/task.sh`
- **THEN** it remains literal text and is not treated as an expression

### Requirement: A task condition controls whether the step runs

The system SHALL evaluate a task's `if` expression against the raw task input before any other expression of that task. The expression result SHALL be a boolean; a non-boolean result SHALL fail the step during execution with a run failure naming the step. When the condition is true, the task SHALL execute normally. When the condition is false, the step SHALL be skipped as a full short-circuit: its body, `then`, `output.as`, and `export` SHALL NOT run, the data value SHALL remain unchanged, and execution SHALL advance to the next step. An `if` expression over a non-JSON raw task input SHALL fail the step during execution with a subject error.

#### Scenario: True condition runs the task

- **WHEN** a call task declares `if: '${ .enabled }'` and the raw input has `enabled: true`
- **THEN** the child task is submitted and the step proceeds normally

#### Scenario: False condition skips the whole task pipeline

- **WHEN** a call task declares `if: '${ .enabled }'`, `output.as: '${ 1 }'`, and `then: exit`, and the raw input has `enabled: false`
- **THEN** no child is submitted, no output transformation runs, no flow directive runs, the data value is unchanged, and the next step begins

#### Scenario: Skipped task preserves the data chain

- **WHEN** a task declaring `if` is skipped and the following step reads the data subject
- **THEN** the following step observes the same data value the skipped task received

#### Scenario: Non-boolean condition fails during execution

- **WHEN** a task declares `if: '${ .count }'` and the raw input has `count: 3`
- **THEN** the workflow fails during execution with an error stating the condition is not a boolean and naming the step

#### Scenario: Condition over non-JSON input fails

- **WHEN** a task declares `if` and the raw task input is opaque bytes
- **THEN** the workflow fails during execution with an error stating the subject is not JSON and naming the step

#### Scenario: Malformed condition fails before submitting

- **WHEN** a task declares an `if` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted

### Requirement: Task input.from transforms the task's input

When a task declares `input.from`, the system SHALL evaluate it against the raw task input before the task body runs, exactly once per execution of that step. The result SHALL be JSON-faithful: a string result SHALL be a JSON string rather than opaque bytes. The transformed input SHALL be the subject of the task's own expressions, including a `call`'s `with` values, and the seed of a nested `do` or `fork` sequence. When the task declares no `input.from`, the transformed input SHALL be the raw task input.

#### Scenario: Transformed input is the call argument subject

- **WHEN** a task declares `input.from: '${ .payload }'` and a `with` parameter expression references `.id`
- **THEN** the parameter expression is evaluated with `.id` taken from the payload, not from the raw task input

#### Scenario: Transformed input seeds a nested do

- **WHEN** a `do` task declares `input.from` and its nested sequence's first step reads the subject
- **THEN** the nested step observes the transformed input

#### Scenario: Transformed input seeds a fork

- **WHEN** a `fork` task declares `input.from` and its branches read the subject
- **THEN** every branch begins with a copy of the transformed input

#### Scenario: No input.from is the identity

- **WHEN** a task declares no `input.from`
- **THEN** the transformed input is the raw task input, unchanged

#### Scenario: Transformed input preserves a JSON string

- **WHEN** `input.from` evaluates to the string `world`
- **THEN** the transformed input is the JSON string `"world"`, not the opaque bytes `world`

#### Scenario: Malformed input transformation fails before submitting

- **WHEN** a task declares an `input.from` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted

### Requirement: Task output.as transforms the step's raw output

When a completed `call`, `do`, or `fork` step declares `output.as`, the system SHALL evaluate it against the step's raw output and use its JSON-faithful result as the data value for the following step. The raw output SHALL be the expression subject; `$output` SHALL name that same raw output, and `$input` SHALL name the step's transformed input. When the step declares no `output.as`, its raw output SHALL become the data value. A declared `output.as` SHALL require both the transformed input and the raw output to be JSON; if either is opaque, the step SHALL fail during execution with a subject error naming the step.

#### Scenario: Call output is shaped

- **WHEN** a call step returns `{"id":7,"name":"pet"}` and declares `output.as: '${ .id }'`
- **THEN** the following step's raw task input is `7`

#### Scenario: Nested sequence output is shaped

- **WHEN** a `do` task completes and declares `output.as`
- **THEN** the transformation applies to the nested sequence's final value

#### Scenario: Fork join output is shaped

- **WHEN** a `fork` task completes and declares `output.as`
- **THEN** the transformation applies to the join value

#### Scenario: Output expression reads $input and $output

- **WHEN** a task declares `output.as: '${ { before: $input, after: $output } }'`
- **THEN** `$input` is the transformed task input and `$output` is the raw task output

#### Scenario: No output.as preserves the raw output

- **WHEN** a step declares no `output.as`
- **THEN** the raw output, including opaque bytes, becomes the data value unchanged

#### Scenario: Output transformation over opaque output fails

- **WHEN** a step declares `output.as` and its raw output is not JSON
- **THEN** the workflow fails during execution with an error stating the subject is not JSON and naming the step

#### Scenario: Output transformation over opaque input fails

- **WHEN** a step declares `output.as` and its transformed input is not JSON
- **THEN** the workflow fails during execution with an error stating the input is not JSON and naming the step

#### Scenario: Malformed output transformation fails before submitting

- **WHEN** a task declares an `output.as` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted

### Requirement: Document output.as transforms the workflow's final output

When a workflow completes successfully and the document declares `output.as`, the system SHALL evaluate it against the last transformed data value and use its JSON-faithful result as the final result body. The last data value SHALL be the expression subject, and `$context` SHALL remain present and empty. When the document declares no `output.as`, the last data value SHALL be the final result body. A declared `output.as` over a final value that is not JSON SHALL fail the workflow during execution with a subject error.

#### Scenario: Document output shapes the final result

- **WHEN** the last step's transformed output is `{"id":7}` and the document declares `output.as: '${ { petId: .id } }'`
- **THEN** the final result body is `{"petId":7}`

#### Scenario: No document output preserves the last value

- **WHEN** the document declares no `output.as`
- **THEN** the final result body is the last data value, including opaque bytes

#### Scenario: Document output over non-JSON fails

- **WHEN** the document declares `output.as` and the last data value is not JSON
- **THEN** the workflow fails during execution with an error stating the subject is not JSON

#### Scenario: Malformed document output fails before submitting

- **WHEN** the document declares an `output.as` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted
