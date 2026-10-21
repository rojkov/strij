# openworkflow-interpreter

## Purpose

Defines how a nodeagent executes a workflow document loaded from a function artifact: the resumable step loop, the data chain seeded from the task body as invocation input and carrying values between steps as the expression subject, the policy for evaluating expressions against it, the split between validation that is possible without data and validation that is not, and how the workflow's own success and failure are reported.

## Requirements

### Requirement: A workflow document is executed as a resumable step sequence

The system SHALL execute a workflow document's top-level `do` body as an ordered sequence of tasks, advancing one task at a time. Nested `do` bodies and `fork` branches SHALL execute as nested sequences at the position of the task that declares them. A step that must await a result SHALL suspend its sequence and the workflow SHALL resume within that sequence once that result arrives. Steps belonging to different `fork` branches SHALL be able to await their results concurrently. The system SHALL NOT require the whole body to complete within the call that started the workflow.

#### Scenario: Body runs in declaration order

- **WHEN** a workflow body declares three steps in order and each completes without suspending on external work
- **THEN** the steps execute in declaration order and the workflow completes after the third

#### Scenario: Execution suspends across a child result

- **WHEN** a step submits a child task and suspends awaiting its result
- **THEN** no further step begins until that result arrives, and execution resumes at the step following the suspended one

#### Scenario: Local child completing during submission does not deadlock

- **WHEN** a step's child task is admitted locally and its result is delivered before the submission call returns
- **THEN** the step records that result and execution continues without waiting for a second delivery

#### Scenario: Nested do body executes at its position

- **WHEN** a task in the body declares a nested `do` sequence
- **THEN** that sequence's steps execute at the position of the nested task, and the step following the nested task runs after the nested sequence completes

#### Scenario: A fork suspends its enclosing sequence

- **WHEN** a task declares a fork whose branches are still outstanding
- **THEN** no task following the fork in the enclosing sequence begins until the fork's join has resolved

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

### Requirement: In strict mode, only `${ }`-wrapped strings are expressions

The system SHALL treat a string as a runtime expression if and only if it is wrapped in `${ }`, and SHALL treat every other string as literal text. This rule SHALL apply uniformly at every string position, whether that position is a top-level value, a value nested in an object, or a value nested in an array. A non-string value SHALL never be evaluated.

The document's `evaluate.mode` SHALL govern this. When the document does not declare an `evaluate` section, or declares it without a mode, the mode SHALL be `strict`. Under `strict`, the rule above applies. `loose` mode is not part of this capability.

#### Scenario: Wrapped string is a program at the top level

- **WHEN** a value slot's entire value is the string `${ .field }`
- **THEN** it is evaluated as a program

#### Scenario: Wrapped string is a program when nested

- **WHEN** a value slot is an object whose member's value is the string `${ .field }`
- **THEN** that member is evaluated as a program

#### Scenario: Unwrapped string is literal at the top level

- **WHEN** a value slot's entire value is the string `.field`
- **THEN** it is the literal text `.field` and is not evaluated

#### Scenario: Unwrapped string is literal when nested

- **WHEN** a value slot is an object whose member's value is the string `.field`
- **THEN** that member is the literal text `.field` and is not evaluated

#### Scenario: Non-strings are never evaluated

- **WHEN** a value slot holds a number, a boolean, or null
- **THEN** it is used as that value and no expression is evaluated

#### Scenario: Arrays recurse into their members

- **WHEN** a value slot is an array containing a `${ }`-wrapped string and an unwrapped string
- **THEN** the first element is evaluated and the second is retained as literal text

#### Scenario: The whole value is one expression, not a template

- **WHEN** a value slot's entire value is the string `${ "a\(.)b" }`
- **THEN** the entire value is that one expression and its whole result is the value, rather than literal text surrounding an interpolated fragment

#### Scenario: An omitted mode defaults to strict

- **WHEN** a document declares no `evaluate` section, or an `evaluate` section with no mode
- **THEN** mode `strict` applies

#### Scenario: Loose mode is not accepted

- **WHEN** a document declares mode `loose`
- **THEN** the workflow fails with an error stating that `loose` mode is not supported

#### Scenario: Expression over non-JSON data fails with a subject error

- **WHEN** a step declares an expression and the data value it reads is not JSON
- **THEN** the workflow fails with an error stating the subject is not JSON, naming the step

#### Scenario: Expression syntax error is distinct from a subject error

- **WHEN** a step declares an expression that is not a valid program
- **THEN** the workflow fails with an error reporting invalid expression syntax, distinguishable from the non-JSON subject error

#### Scenario: Literal override bypasses evaluation

- **WHEN** a value slot is given a literal instead of a `${ }`-wrapped expression
- **THEN** the literal is used as-is and no expression is evaluated

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

### Requirement: Workflow success and failure are reported on the originating connection

The system SHALL deliver exactly one final result to the connection that submitted the workflow, whether the workflow completes or fails. On success the final result SHALL carry the workflow's output value and SHALL be marked final. On failure the final result SHALL carry a non-OK `TaskResult.status`, SHALL carry in its body a message naming the step that failed, and SHALL rely on the node's result-delivery path to surface the status to the originating connection as a failure rather than a success.

#### Scenario: Successful workflow delivers one final result

- **WHEN** every step completes
- **THEN** the originating connection receives exactly one final result carrying the workflow's output value

#### Scenario: Failed workflow reports the failing step

- **WHEN** a step fails
- **THEN** the originating connection is notified of the failure with a message naming the step, and no further steps execute

#### Scenario: A failed workflow carries a non-OK status

- **WHEN** a workflow fails
- **THEN** its final result SHALL have a status that reads as failure, so a consumer applying the success predicate does not interpret the error message as an output value

#### Scenario: The failure status distinguishes malformed, unsupported, and run failures

- **WHEN** a workflow fails because its document did not parse or failed pre-execution validation, because it requested a recognized-but-unsupported call target, or because a step failed during execution
- **THEN** each case SHALL carry the corresponding distinct status code rather than a single shared failure code

#### Scenario: Failure after a suspension still reports once

- **WHEN** a step suspended awaiting a child and the child is refused
- **THEN** the workflow reports the failure exactly once and does not wait indefinitely for the refused child

#### Scenario: Originating connection closes mid-run

- **WHEN** the originating connection is torn down while the workflow is suspended
- **THEN** the workflow stops without delivering a further result, and children still outstanding are abandoned

### Requirement: A fork task executes its branches as concurrent nested sequences

A fork task's branches SHALL execute as nested ordered sequences. Each branch SHALL begin with a copy of the data value current at the fork step, and that branch's steps SHALL carry values along this copied chain. A branch task MAY itself be a call, a do, or another fork; any other task kind inside a fork SHALL be reported the same way it is outside a fork. The sequence containing the fork SHALL remain suspended until the fork's join has resolved.

#### Scenario: Branches advance concurrently

- **WHEN** a fork declares three branches and each branch's first step awaits a child result
- **THEN** all three children are submitted before the fork resolves, and each branch's steps advance on that branch's own chain

#### Scenario: A multi-step branch keeps its own chain

- **WHEN** a branch declares two sequential calls
- **THEN** the second call's expression subject is the first call's result within that branch, independent of the other branches' results

#### Scenario: A nested fork's join is its branch's result

- **WHEN** a branch declares a fork task
- **THEN** the inner fork's join value becomes the value that branch contributes to the outer join

### Requirement: The fork join produces the value that continues the data chain

When the fork does not set `compete`, or sets it to false, the join value SHALL be a JSON array of every branch's result in declaration order. When the fork sets `compete: true`, the join value SHALL be the result of the first branch to reach a terminal outcome; outcomes of the remaining branches arriving after that point SHALL be ignored. Under join-all, a branch result that is not a JSON value SHALL fail the workflow at the join site with a failure naming that branch; the system SHALL NOT invent a representation for opaque branch results.

#### Scenario: Join-all yields an ordered array

- **WHEN** a fork with three branches completes all of them and the document does not set `compete`
- **THEN** the join value, and the expression subject of the task following the fork, is a JSON array of the three branch results in declaration order

#### Scenario: Compete yields the winner's result

- **WHEN** a fork with `compete: true` completes one branch while others are still running
- **THEN** the join value is that branch's result, and the task following the fork receives it as its data value

#### Scenario: Late outcomes after the winner are ignored

- **WHEN** a fork with `compete: true` has resolved on one branch's outcome and another branch's outcome arrives afterwards
- **THEN** the late outcome changes nothing: the join value and the workflow's course remain as decided

#### Scenario: An opaque branch result fails the join

- **WHEN** every branch of a join-all fork completes, and one branch's result is not JSON
- **THEN** the workflow fails at the join with a failure naming that branch, rather than producing an array

### Requirement: A fork failure ends the workflow immediately

The first terminal branch outcome that is a failure SHALL fail the workflow with that failure's status and a message naming the branch, and the remaining branches SHALL be abandoned. This applies identically under join-all and under `compete: true`; under `compete: true` the first terminal outcome — success or failure — is what decides the fork.

#### Scenario: Join-all fails fast on one branch failure

- **WHEN** a join-all fork has one branch fail while sibling branches are still running
- **THEN** the workflow fails with that branch's status, naming the branch, without waiting for the siblings to finish

#### Scenario: Compete can be decided by a failure

- **WHEN** a fork with `compete: true` has its first terminal outcome be a branch failure, even though another branch might succeed later
- **THEN** the workflow fails with that branch's status, naming the branch

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
