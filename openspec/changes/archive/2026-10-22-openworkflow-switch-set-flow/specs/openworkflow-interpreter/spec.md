# Spec Delta

## ADDED Requirements

### Requirement: Task flow directives control the next step

A completed task's `then` SHALL determine the next step in the task's own sequence. `continue`, or the absence of `then`, SHALL advance to the next declared task. A task name SHALL continue execution at that named task in the same sequence; the target MAY be declared before or after the current task, and a name SHALL NOT resolve to a task in a different sequence or scope. `exit` SHALL complete the current sequence; when that sequence is the workflow's top-level sequence, the workflow SHALL conclude. `end` SHALL conclude the workflow from any depth.

The directive SHALL run after the step's `output.as` and `export.as` effects. It SHALL NOT run for a step skipped by `if` or for a failed step. An `end` directive SHALL conclude the workflow without applying the `output.as` or `export.as` of an enclosing `do` or `fork` step that has not completed. Document `output.as` SHALL still shape the resulting final value.

#### Scenario: Continue advances to the next declared task

- **WHEN** a task completes with no `then` or with `then: continue`
- **THEN** the following declared task in the same sequence runs next

#### Scenario: A named directive jumps forward

- **WHEN** a task completes with `then` naming a later task in its sequence
- **THEN** execution continues at that task, skipping the tasks declared between them

#### Scenario: A named directive jumps backward

- **WHEN** a task completes with `then` naming an earlier task in its sequence
- **THEN** execution continues at that earlier task with the current data value as its raw input

#### Scenario: Exit completes the current nested sequence

- **WHEN** a task inside a nested `do` body completes with `then: exit`
- **THEN** the remaining tasks in that `do` body are skipped, the `do` task completes, and its enclosing sequence continues

#### Scenario: Exit in the top-level sequence concludes the workflow

- **WHEN** a task in the document's top-level `do` completes with `then: exit`
- **THEN** the remaining top-level tasks are skipped and the workflow concludes with the step's transformed output

#### Scenario: End from a nested sequence concludes the workflow

- **WHEN** a task inside a nested `do` or fork branch completes with `then: end`
- **THEN** the workflow concludes with that step's transformed output, the remaining steps and branches are abandoned, and no enclosing `do` or `fork` applies its own `output.as` or `export.as`

#### Scenario: A skipped step does not run its directive

- **WHEN** a task with `then` is skipped because its `if` condition is false
- **THEN** no flow directive runs, the data value is unchanged, and the next declared task runs

#### Scenario: A failed step does not run its directive

- **WHEN** a task's body, `output.as`, or `export.as` fails
- **THEN** the workflow fails and the task's `then` does not run

### Requirement: A switch task selects a flow directive from its cases

A `switch` task SHALL evaluate its cases against the transformed task input. Cases that declare `when` SHALL be considered in declaration order, and the first case whose expression yields `true` SHALL select that case. A case without `when` SHALL be a fallback selected only if no conditional case matched. The selected case's `then` SHALL be the step's flow directive. If no case matches, the switch task's own `then` SHALL be used, or `continue` when it is absent. The switch task's raw output SHALL be its transformed task input, and `output.as`, `export.as`, and `then` SHALL apply through the ordinary step pipeline.

A case `when` SHALL be an expression-typed slot and SHALL yield a boolean. A non-boolean result SHALL fail the step during execution, naming the task and case. When at least one case declares `when`, a non-JSON transformed input SHALL fail the step during execution with a subject error; a switch with no conditional case SHALL not require a JSON subject. Every case SHALL declare `then`, and a switch SHALL declare at most one case without `when`; violations SHALL be malformed documents rejected before execution.

#### Scenario: The first matching case is selected

- **WHEN** multiple cases declare `when` expressions and more than one yields `true`
- **THEN** only the first such case in declaration order is selected

#### Scenario: The fallback case handles an unmatched input

- **WHEN** no conditional case matches and a case without `when` is declared
- **THEN** that fallback case is selected and its `then` is applied

#### Scenario: The fallback is considered after all conditional cases

- **WHEN** a case without `when` is declared before a conditional case that matches
- **THEN** the matching conditional case is selected and the fallback is not

#### Scenario: The switch's own then handles no match

- **WHEN** no case matches and the switch task declares `then`
- **THEN** the switch task's `then` is applied

#### Scenario: No match and no switch then continues

- **WHEN** no case matches, no fallback case exists, and the switch task declares no `then`
- **THEN** execution continues at the next declared task

#### Scenario: The switch output is its transformed input

- **WHEN** a switch declares `input.from` and no `output.as`
- **THEN** the data value following the switch is the transformed input, including when a case was selected

#### Scenario: A non-boolean condition fails during execution

- **WHEN** a case's `when` expression yields a non-boolean value
- **THEN** the workflow fails during execution, naming the switch task and the case

#### Scenario: A conditional case over opaque input fails

- **WHEN** a switch declares at least one conditional case and the transformed input is not JSON
- **THEN** the workflow fails during execution with a subject error naming the switch task

#### Scenario: A default-only switch needs no JSON subject

- **WHEN** a switch declares only cases without `when` and the transformed input is opaque
- **THEN** the fallback case is selected without a subject error

#### Scenario: More than one default case is malformed

- **WHEN** a switch declares more than one case without `when`
- **THEN** the workflow fails in the validation pass with a malformed-document error

#### Scenario: A case without then is malformed

- **WHEN** a switch case declares no `then`
- **THEN** the workflow fails in the validation pass with a malformed-document error

### Requirement: A set task replaces the data chain

A `set` task SHALL evaluate its data against the transformed task input and use the JSON-faithful result as its raw output. The ordinary step pipeline SHALL then apply `output.as`, `export.as`, and `then`. The data SHALL be either an object or a single scalar. Each string in an object SHALL be an expression if and only if it is wholly wrapped in `${ }`; a partial wrapper SHALL be rejected, and non-string members SHALL be preserved. A scalar value SHALL follow the same rule: a wholly wrapped scalar SHALL be evaluated and a bare scalar SHALL be literal text.

`set` expressions SHALL bind the workflow context; a `set` task SHALL NOT modify the context. A `set` result SHALL always be JSON.

#### Scenario: Object members are evaluated and literals preserved

- **WHEN** a `set` task declares an object whose members mix bare strings and whole `${ }` expressions
- **THEN** the expressions are evaluated against the transformed input, the bare strings remain literal, and the object becomes the data value

#### Scenario: A scalar expression replaces the data chain

- **WHEN** a `set` task declares `set: '${ .configuration.color }'`
- **THEN** the expression is evaluated against the transformed input and its JSON value becomes the data value

#### Scenario: A bare scalar is literal text

- **WHEN** a `set` task declares a scalar that is not wholly wrapped in `${ }`
- **THEN** that text is used as a JSON string value

#### Scenario: A set result is shaped by output.as

- **WHEN** a `set` task declares both `set` and `output.as`
- **THEN** `output.as` is evaluated against the set value and its result becomes the data value

#### Scenario: A set task does not modify the context

- **WHEN** a `set` task executes without `export`
- **THEN** `$context` is unchanged

#### Scenario: An expression over opaque input fails

- **WHEN** a `set` value contains an expression and the transformed task input is not JSON
- **THEN** the workflow fails during execution with a subject error naming the task

#### Scenario: A malformed set expression fails before execution

- **WHEN** a `set` value contains a partially wrapped string or an expression that is not a valid program
- **THEN** the workflow fails in the validation pass with a malformed-document error

### Requirement: Synchronous step execution is bounded

The system SHALL bound the number of steps a sequence advances in one synchronous stretch, because flow directives may jump backward and create a loop that never suspends. When that bound is exceeded, the workflow SHALL fail with `INTERNAL`, naming the step being advanced, instead of continuing to execute. The bound SHALL be implementation-defined and generous enough not to affect ordinary workflows. Accounting SHALL begin again when a sequence resumes after awaiting a child, so a loop that suspends on a child task is not bounded by this rule.

#### Scenario: A synchronous self-jump fails instead of hanging

- **WHEN** a task declares `then` naming itself and executes no child task
- **THEN** the workflow fails with an `INTERNAL` status after exceeding the synchronous bound, naming the step

#### Scenario: A synchronous cycle across tasks fails

- **WHEN** two or more tasks in one sequence redirect to each other and none awaits a child
- **THEN** the workflow fails with an `INTERNAL` status instead of advancing forever

#### Scenario: A bounded synchronous sequence completes normally

- **WHEN** a sequence advances fewer steps than the bound before completing or suspending
- **THEN** it behaves as specified without a guard failure

## MODIFIED Requirements

### Requirement: The data chain carries one value between steps and is the expression subject

The system SHALL maintain a single data value that flows from step to step, which SHALL be either a JSON value or an opaque byte value. A step's raw task input SHALL be that data value. Expressions SHALL receive the step's transformed task input as their subject: the JSON-faithful result of the task's `input.from` when declared, otherwise the raw task input.

The system SHALL seed that data value from the workflow's invocation input, which is the task body. An empty body SHALL seed JSON `null`. A non-empty body SHALL seed the data value from its bytes: JSON when they parse as JSON, opaque otherwise. If the document declares `input.from`, the system SHALL evaluate it with the invocation input as its subject and use its JSON-faithful result as the seed instead; a string result SHALL be a JSON string rather than opaque bytes. The raw task input at a step SHALL be the value produced by the step before it.

The DSL's `$context` SHALL NOT be confused with this subject. `$context` is a separate workflow-scope variable, seeded as an empty JSON object and replaced only by a task's `export.as`. It SHALL persist across steps and be available to runtime expressions throughout the run. It SHALL NOT observe the data chain. A `set` task SHALL write the data chain through its raw output and SHALL NOT modify `$context`. Document `input.from` runs before any export and therefore observes the empty seed.

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

#### Scenario: A set task replaces the data chain

- **WHEN** a step is a `set` task and the following step reads the data subject
- **THEN** the following step observes the `set` task's JSON-faithful result as its raw task input

### Requirement: Validation that does not depend on data completes before any child is submitted

The system SHALL perform a validation pass over the document before executing its first step. That pass SHALL cover the facts derivable from the document alone: the call target of each call, the key set of each call's arguments, the value kind of each declared requirement, the syntax and whole-value wrapping of every runtime expression (including each task's `if`, `input.from`, `output.as`, `export.as`, and `switch` case `when`, and the document's `input.from` and `output.as`), the presence of `then` on every switch case, at most one switch case without `when`, the syntactic form of each `set` data value, and the resolution of every `then` directive to `continue`, `exit`, `end`, or a task name declared in the same sequence. The pass SHALL reject duplicate task names within one sequence. The pass SHALL cover every task in every nested sequence, including the branch tasks of every fork; each fork branch's single task SHALL be validated in its own one-task sequence scope. A fork declared to compete with no branches SHALL be rejected in this pass, and a task that declares `export` inside a fork branch SHALL be rejected in this pass as unimplemented. A failure in this pass SHALL be reported before any child task is submitted. Failures that depend on data SHALL instead occur during execution.

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

#### Scenario: A malformed switch fails before submitting anything

- **WHEN** a switch case declares an invalid `when` expression, declares no `then`, or more than one case omits `when`
- **THEN** the workflow fails with a malformed-document error and no child task has been submitted

#### Scenario: An unresolved flow directive fails before submitting anything

- **WHEN** a `then` value is not `continue`, `exit`, `end`, or the name of a task in the same sequence
- **THEN** the workflow fails with a malformed-document error and no child task has been submitted

#### Scenario: Duplicate task names in a sequence are malformed

- **WHEN** a sequence declares two tasks with the same name
- **THEN** the workflow fails with a malformed-document error and no child task has been submitted

#### Scenario: A malformed set value fails before submitting anything

- **WHEN** a `set` value contains a partially wrapped string or an expression that is not a valid program
- **THEN** the workflow fails with a malformed-document error and no child task has been submitted

### Requirement: Expression-typed slots accept only whole-value wrapped expressions

For the expression-typed slots — a task's `if`, a task's `input.from`, a task's `output.as`, a task's `export.as`, a `switch` case's `when`, the document's `input.from`, and the document's `output.as` — the system SHALL treat only a string wholly wrapped in `${ }` as an expression. A bare string in one of these slots SHALL be rejected as a malformed document rather than treated as literal text. When the slot uses an object form, each nested string member SHALL follow the same rule. Literal-capable value slots, such as a call's `with` values, a `set` task's data, and task `metadata`, SHALL retain the literal-text rule; a bare string in a `set` value is literal text.

#### Scenario: Bare condition is malformed

- **WHEN** a task declares `if: .enabled`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare switch condition is malformed

- **WHEN** a switch case declares `when: .orderType == "electronic"`
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

- **WHEN** a task declares `if: '${ .enabled }'`, `input.from: '${ .payload }'`, `output.as: '${ .result }'`, and `export.as: '${ { token: .token } }'`, and a switch case declares `when: '${ .orderType == "electronic" }'`
- **THEN** the document parses, validates, and executes those slots as expressions

#### Scenario: Literal value slots are unaffected

- **WHEN** a call's `with` value or a `set` value is the unwrapped string `/bin/task.sh`
- **THEN** it remains literal text and is not treated as an expression

### Requirement: A fork task executes its branches as concurrent nested sequences

A fork task's branches SHALL execute as nested ordered sequences. Each branch SHALL begin with a copy of the data value current at the fork step, and that branch's steps SHALL carry values along this copied chain. A branch task MAY itself be a call, a do, a fork, a set, or a switch; any other task kind inside a fork SHALL be reported the same way it is outside a fork. Because a branch is a single task, a branch that needs multiple steps or a choice among alternative steps SHALL declare a `do`; a branch that is directly a `switch` SHALL resolve its case directives only within its own one-task scope. The sequence containing the fork SHALL remain suspended until the fork's join has resolved.

#### Scenario: Branches advance concurrently

- **WHEN** a fork declares three branches and each branch's first step awaits a child result
- **THEN** all three children are submitted before the fork resolves, and each branch's steps advance on that branch's own chain

#### Scenario: A multi-step branch keeps its own chain

- **WHEN** a branch declares two sequential calls
- **THEN** the second call's expression subject is the first call's result within that branch, independent of the other branches' results

#### Scenario: A nested fork's join is its branch's result

- **WHEN** a branch declares a fork task
- **THEN** the inner fork's join value becomes the value that branch contributes to the outer join

#### Scenario: A set branch runs on its branch chain

- **WHEN** a branch declares a `set` task
- **THEN** that task runs on the branch's own chain and contributes its result to the join like a call or `do`

#### Scenario: A multi-step branch declares a do

- **WHEN** a branch needs to run several tasks sequentially or select among alternative steps
- **THEN** the branch task is a `do` whose body contains those tasks, and flow directives inside it resolve within that body

#### Scenario: A direct switch branch is limited to its one-task scope

- **WHEN** a fork declares a `switch` task directly as a branch
- **THEN** its case directives may only continue, exit, end, or target the switch itself; `continue` and `exit` both complete the branch, `end` concludes the workflow, and a case naming a task in another branch is malformed
