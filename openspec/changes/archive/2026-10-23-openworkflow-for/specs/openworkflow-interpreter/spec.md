# Spec Delta

## ADDED Requirements

### Requirement: A for task iterates a collection with a carried accumulator

A `for` task SHALL evaluate its `for.in` collection exactly once, against the task's transformed input, before its first iteration, and SHALL execute its `do` body once per element in declaration order. The first iteration SHALL begin with the task's transformed input; each completed iteration's data value SHALL seed the next; and the task's raw output SHALL be the accumulator after the final iteration. When the collection is empty, the raw output SHALL be the transformed input.

`for.in` SHALL accept two forms. A string SHALL be an expression-typed slot: a whole-value `${ }` expression evaluated against the transformed input. A non-string SHALL be a literal-capable value tree evaluated against the transformed input: `${ }`-wrapped strings are evaluated, bare strings remain literal, and the resulting array is the collection. The evaluated collection SHALL be an array or `null`; an array is iterated in order, `null` produces zero iterations, and any other result SHALL fail the step during execution with a failure naming the task. An expression over a non-JSON transformed input SHALL fail the step with a subject error.

`for.each` and `for.at` SHALL name the current element and zero-based index. An absent, empty, or whitespace-only `each` defaults to `item`, and an absent, empty, or whitespace-only `at` defaults to `index`; a leading `$` on a declared name is not part of the variable name. The bound variables SHALL be expression bindings scoped to the body: visible to every expression in every task of the body, including nested `do`, `fork`, and `for` bodies, and preserved across a suspension. They SHALL NOT be visible in the `for` task's own `if`, `input.from`, `output.as`, `export.as`, or `then`; `for.in` SHALL observe only loop variables of enclosing loops. A nested `for` that reuses a name SHALL shadow the enclosing binding within its body. The declared names SHALL be valid identifiers and SHALL differ from each other and from the reserved `context`, `input`, and `output` names; a violation SHALL be a malformed document.

After each completed iteration, if the task declares `while`, the system SHALL evaluate `while` against the accumulator, with the current loop variables and `$context` bound, and the result SHALL be a boolean. A false result SHALL stop iteration before the next element and leave the accumulator as the task's raw output. `while` SHALL be an expression-typed slot; a non-boolean result, a subject that is not JSON, or a malformed program SHALL fail the step as specified for expression slots. An `end` flow directive inside the body SHALL conclude the workflow immediately and skip the `for` task's own `output.as`, `export.as`, and `then`; an `exit` directive SHALL complete only the current iteration's body.

A failure while evaluating `in` or `while`, or in any body step, SHALL fail the workflow exactly once and SHALL NOT continue to later iterations. The `for` task SHALL otherwise pass through the ordinary step pipeline: `if` may skip the whole loop, `input.from` seeds it, and `output.as`, `export.as`, and `then` apply after iteration completes.

#### Scenario: Iterations run in declaration order with a carried accumulator

- **WHEN** the transformed input is `{"colors":["red","green","blue"]}`, `for.in` selects `.colors`, and the body appends each `$color` and `$index` to the accumulator
- **THEN** every element is visited in declaration order, the final raw output is `{"processed":{"colors":["red","green","blue"],"indexes":[0,1,2]}}`, and the following step receives it as its raw input

#### Scenario: Default loop variables are item and index

- **WHEN** a `for` task omits `each` and `at` and its body reads `$item` and `$index`
- **THEN** the current element and its zero-based position are bound under those names

#### Scenario: Custom loop variable names are bound

- **WHEN** a `for` task declares `each: $color` and `at: position` and its body reads `$color` and `$position`
- **THEN** the leading `$` is ignored for the declared names and the element and index are bound as those variables

#### Scenario: Nested loops see enclosing and inner variables

- **WHEN** an inner `for` runs inside the body of an outer `for` and the inner body reads both loop variables
- **THEN** both bindings are visible, and an inner name that reuses an outer name shadows the outer binding within the inner body

#### Scenario: An empty collection preserves the transformed input

- **WHEN** `for.in` evaluates to an empty array
- **THEN** no body step runs and the task's raw output is its transformed input

#### Scenario: A null collection produces zero iterations

- **WHEN** `for.in` evaluates to `null`
- **THEN** no body step runs and the task's raw output is its transformed input

#### Scenario: A non-array collection fails during execution

- **WHEN** `for.in` evaluates to a value that is neither an array nor `null`
- **THEN** the workflow fails during execution with a failure naming the task, and no body step runs

#### Scenario: An inline array is the collection

- **WHEN** `for.in` is written as an inline array containing literal values and `${ }` expressions
- **THEN** the wrapped strings are evaluated against the transformed input, the bare values stay literal, and the resulting array is iterated in order

#### Scenario: An expression collection over opaque input fails

- **WHEN** the string form of `for.in` is an expression and the task's transformed input is not JSON
- **THEN** the workflow fails during execution with a subject error naming the task

#### Scenario: A suspended body keeps its loop variables

- **WHEN** a body step submits a child, suspends, and its `output.as` expression references `$item`
- **THEN** after the outcome arrives the output is shaped with the same item and the loop continues from that accumulator

#### Scenario: While stops before the next element

- **WHEN** `for.while` is declared and its expression yields `false` after an iteration
- **THEN** no further elements are visited and the accumulator after that iteration is the task's raw output

#### Scenario: A non-boolean while result fails the step

- **WHEN** `for.while` evaluates to a non-boolean value
- **THEN** the workflow fails during execution with an error naming the task

#### Scenario: While over an opaque accumulator fails

- **WHEN** `for.while` is declared and the accumulator is not JSON
- **THEN** the workflow fails during execution with a subject error naming the task

#### Scenario: End inside a for body concludes the workflow

- **WHEN** a task inside a `for` body completes with `then: end`
- **THEN** the workflow concludes with that task's transformed output, later iterations do not run, and the `for` task's own `output.as` and `export.as` do not run

#### Scenario: Exit inside a for body completes only the current iteration

- **WHEN** a task inside a `for` body completes with `then: exit`
- **THEN** the remaining steps of that iteration are skipped and the next element begins

## MODIFIED Requirements

### Requirement: A workflow document is executed as a resumable step sequence

The system SHALL execute a workflow document's top-level `do` body as an ordered sequence of tasks, advancing one task at a time. Nested `do` bodies, `for` bodies, and `fork` branches SHALL execute as nested sequences at the position of the task that declares them. A step that must await a result SHALL suspend its sequence and the workflow SHALL resume within that sequence once that result arrives. Steps belonging to different `fork` branches SHALL be able to await their results concurrently. The system SHALL NOT require the whole body to complete within the call that started the workflow.

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

#### Scenario: A for body executes at its position

- **WHEN** a task declares a `for` whose body contains steps
- **THEN** those steps execute at the position of the `for` task, every iteration completes before the following task begins, and a suspension inside an iteration resumes within that iteration

#### Scenario: A fork suspends its enclosing sequence

- **WHEN** a task declares a fork whose branches are still outstanding
- **THEN** no task following the fork in the enclosing sequence begins until the fork's join has resolved

### Requirement: The data chain carries one value between steps and is the expression subject

The system SHALL maintain a single data value that flows from step to step, which SHALL be either a JSON value or an opaque byte value. A step's raw task input SHALL be that data value. Expressions SHALL receive the step's transformed task input as their subject: the JSON-faithful result of the task's `input.from` when declared, otherwise the raw task input.

The system SHALL seed that data value from the workflow's invocation input, which is the task body. An empty body SHALL seed JSON `null`. A non-empty body SHALL seed the data value from its bytes: JSON when they parse as JSON, opaque otherwise. If the document declares `input.from`, the system SHALL evaluate it with the invocation input as its subject and use its JSON-faithful result as the seed instead; a string result SHALL be a JSON string rather than opaque bytes. The raw task input at a step SHALL be the value produced by the step before it.

A `for` task SHALL carry its body's data value across iterations: the first iteration SHALL begin with the task's transformed input, each completed iteration's value SHALL seed the next, and the task's raw output SHALL be the final accumulator. An empty or `null` collection SHALL leave the transformed input as the accumulator.

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

#### Scenario: A for task carries its accumulator across iterations

- **WHEN** a `for` body replaces the data value on each iteration
- **THEN** the next iteration's raw task input is the previous iteration's result, and the task following the `for` observes the final accumulator

### Requirement: Validation that does not depend on data completes before any child is submitted

The system SHALL perform a validation pass over the document before executing its first step. That pass SHALL cover the facts derivable from the document alone: the call target of each call, the key set of each call's arguments, the value kind of each declared requirement, the syntax and whole-value wrapping of every runtime expression (including each task's `if`, `input.from`, `output.as`, `export.as`, each `switch` case's `when`, each `for` task's `while` and the string form of its `in`, and the document's `input.from` and `output.as`), the presence of `then` on every switch case, at most one switch case without `when`, the syntactic form of each `set` data value and each non-string `for.in` value, the loop variable names declared by each `for` task, and the resolution of every `then` directive to `continue`, `exit`, `end`, or a task name declared in the same sequence. The pass SHALL reject duplicate task names within one sequence. The pass SHALL cover every task in every nested sequence, including the branch tasks of every fork and the body tasks of every `for`; each fork branch's single task SHALL be validated in its own one-task sequence scope, and a `for` body SHALL be validated in its own sequence scope with that task's loop variable names visible. A `for` task's declared names SHALL be valid identifiers, SHALL differ from each other, and SHALL NOT be `context`, `input`, or `output`. A fork declared to compete with no branches SHALL be rejected in this pass, and a task that declares `export` inside a fork branch SHALL be rejected in this pass as unimplemented. A failure in this pass SHALL be reported before any child task is submitted. Failures that depend on data SHALL instead occur during execution.

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

#### Scenario: A malformed for expression fails before submitting anything

- **WHEN** a `for` task declares a bare string `in`, a partially wrapped or invalid expression in `in`, or a bare or invalid `while`
- **THEN** the workflow fails with a malformed-document error and no child task has been submitted

#### Scenario: A non-array inline for collection is malformed

- **WHEN** a `for.in` is written as an inline value that is neither an array nor a collection-producing expression
- **THEN** the workflow fails in the validation pass with a malformed-document error and no child task has been submitted

#### Scenario: Invalid loop variable names fail before submitting anything

- **WHEN** a `for` task declares an invalid identifier, the same name for `each` and `at`, or a name that collides with `context`, `input`, or `output`
- **THEN** the workflow fails with a malformed-document error and no child task has been submitted

#### Scenario: A for body expression may reference its loop variables

- **WHEN** a task inside a `for` body declares an expression that references `$item` or `$index`
- **THEN** the validation pass succeeds because those names are in scope for the body

#### Scenario: A malformed task inside a for body fails before submitting anything

- **WHEN** a `for` body declares a task with an unknown call target or an invalid expression
- **THEN** the workflow fails with that error during the validation pass and no child task has been submitted

#### Scenario: A for body inside a fork branch inherits the export rejection

- **WHEN** a task inside a `for` body that is itself inside a fork branch declares `export`
- **THEN** the workflow fails in the validation pass with an unimplemented status and no child task has been submitted

### Requirement: A fork task executes its branches as concurrent nested sequences

A fork task's branches SHALL execute as nested ordered sequences. Each branch SHALL begin with a copy of the data value current at the fork step, and that branch's steps SHALL carry values along this copied chain. A branch task MAY itself be a call, a do, a for, a fork, a set, or a switch; any other task kind inside a fork SHALL be reported the same way it is outside a fork. Because a branch is a single task, a branch that needs multiple steps or a choice among alternative steps SHALL declare a `do`; a branch that is directly a `switch` SHALL resolve its case directives only within its own one-task scope. The sequence containing the fork SHALL remain suspended until the fork's join has resolved.

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

#### Scenario: A for branch runs on its branch chain

- **WHEN** a branch declares a `for` task
- **THEN** the loop runs on the branch's own chain and its final accumulator contributes to the join like a call, `do`, or `set`

#### Scenario: A multi-step branch declares a do

- **WHEN** a branch needs to run several tasks sequentially or select among alternative steps
- **THEN** the branch task is a `do` whose body contains those tasks, and flow directives inside it resolve within that body

#### Scenario: A direct switch branch is limited to its one-task scope

- **WHEN** a fork declares a `switch` task directly as a branch
- **THEN** its case directives may only continue, exit, end, or target the switch itself; `continue` and `exit` both complete the branch, `end` concludes the workflow, and a case naming a task in another branch is malformed

### Requirement: Expression-typed slots accept only whole-value wrapped expressions

For the expression-typed slots — a task's `if`, a task's `input.from`, a task's `output.as`, a task's `export.as`, a `switch` case's `when`, a `for` task's `while`, the string form of a `for` task's `in`, the document's `input.from`, and the document's `output.as` — the system SHALL treat only a string wholly wrapped in `${ }` as an expression. A bare string in one of these slots SHALL be rejected as a malformed document rather than treated as literal text. When the slot uses an object form, each nested string member SHALL follow the same rule. Literal-capable value slots, such as a call's `with` values, a `set` task's data, a non-string `for.in` value, and task `metadata`, SHALL retain the literal-text rule; a bare string in a `set` value or an inline `for.in` array is literal text.

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

#### Scenario: Bare for collection is malformed

- **WHEN** a `for` task declares `in: .colors` rather than a whole `${ }` expression or an inline array
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare while is malformed

- **WHEN** a `for` task declares `while: .vet != null`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Nested object member must be wrapped

- **WHEN** a task declares `input.from: { id: .payload.id }`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Wrapped forms are accepted

- **WHEN** a task declares `if: '${ .enabled }'`, `input.from: '${ .payload }'`, `output.as: '${ .result }'`, and `export.as: '${ { token: .token } }'`, and a switch case declares `when: '${ .orderType == "electronic" }'`
- **THEN** the document parses, validates, and executes those slots as expressions

#### Scenario: Literal value slots are unaffected

- **WHEN** a call's `with` value, a `set` value, or an inline `for.in` array element is the unwrapped string `/bin/task.sh`
- **THEN** it remains literal text and is not treated as an expression

### Requirement: Task input.from transforms the task's input

When a task declares `input.from`, the system SHALL evaluate it against the raw task input before the task body runs, exactly once per execution of that step. The result SHALL be JSON-faithful: a string result SHALL be a JSON string rather than opaque bytes. The transformed input SHALL be the subject of the task's own expressions, including a `call`'s `with` values, and the seed of a nested `do`, `for`, or `fork` sequence. When the task declares no `input.from`, the transformed input SHALL be the raw task input.

#### Scenario: Transformed input is the call argument subject

- **WHEN** a task declares `input.from: '${ .payload }'` and a `with` parameter expression references `.id`
- **THEN** the parameter expression is evaluated with `.id` taken from the payload, not from the raw task input

#### Scenario: Transformed input seeds a nested do

- **WHEN** a `do` task declares `input.from` and its nested sequence's first step reads the subject
- **THEN** the nested step observes the transformed input

#### Scenario: Transformed input seeds a for

- **WHEN** a `for` task declares `input.from` and its body's first step reads the subject
- **THEN** the first iteration observes the transformed input as its raw input

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

When a completed `call`, `do`, `for`, or `fork` step declares `output.as`, the system SHALL evaluate it against the step's raw output and use its JSON-faithful result as the data value for the following step. The raw output SHALL be the expression subject; `$output` SHALL name that same raw output, and `$input` SHALL name the step's transformed input. When the step declares no `output.as`, its raw output SHALL become the data value. A declared `output.as` SHALL require both the transformed input and the raw output to be JSON; if either is opaque, the step SHALL fail during execution with a subject error naming the step.

#### Scenario: Call output is shaped

- **WHEN** a call step returns `{"id":7,"name":"pet"}` and declares `output.as: '${ .id }'`
- **THEN** the following step's raw task input is `7`

#### Scenario: Nested sequence output is shaped

- **WHEN** a `do` task completes and declares `output.as`
- **THEN** the transformation applies to the nested sequence's final value

#### Scenario: For loop output is shaped

- **WHEN** a `for` task completes and declares `output.as`
- **THEN** the transformation applies to the final accumulator

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

### Requirement: Task export.as updates the workflow context

When a task declares `export` with `as`, the system SHALL evaluate `export.as` against the step's transformed task output after the step's output transformation completes and before the next step begins. The transformed output SHALL be the expression subject; `$output` SHALL name that transformed output, `$input` SHALL name the step's transformed input, and `$context` SHALL name the current context. The result SHALL be a JSON object and SHALL replace the workflow context. A task with no `export`, or an `export` with no `as`, SHALL leave the context unchanged.

`export.as` SHALL run only for a step that completed successfully and was not skipped by `if`. A failed or skipped step SHALL NOT export. It SHALL apply to `call`, `do`, `for`, and `fork` steps. A declared `export.as` SHALL require both the transformed input and the transformed output to be JSON; if either is opaque, the step SHALL fail during execution with a subject error naming the step. A result that is not a JSON object SHALL fail the step during execution.

A task that declares `export` inside a fork branch SHALL be rejected during the pre-execution validation pass as unimplemented, because fork branches write concurrently and no context-combination semantics are defined. A `fork` task's own `export`, outside its branches, SHALL be supported, and a `for` body outside a fork branch MAY export on each iteration.

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

- **WHEN** a `do`, `for`, or `fork` task declares `output.as` and `export.as`
- **THEN** `export.as` is evaluated against the shaped output, not the raw nested, accumulated, or join value

#### Scenario: Non-object export result fails

- **WHEN** `export.as` evaluates to an array, scalar, or null
- **THEN** the workflow fails during execution with an error stating the exported context must be an object and naming the step

#### Scenario: Export over opaque values fails

- **WHEN** a task declares `export.as` and its transformed input or transformed output is not JSON
- **THEN** the workflow fails during execution with an error stating the value is not JSON and naming the step

#### Scenario: Export inside a fork branch is rejected

- **WHEN** a task inside a fork branch — directly, in a nested `do`, or in a nested `for` body — declares `export`
- **THEN** the workflow fails in the pre-execution validation pass with an unimplemented status and no child task has been submitted

#### Scenario: Fork task export is allowed

- **WHEN** the task that declares `fork` also declares `export.as`
- **THEN** the export applies to the fork's transformed output after its join resolves

#### Scenario: For task export is allowed outside a fork branch

- **WHEN** a `for` task outside a fork branch declares `export.as`
- **THEN** the export applies to the loop's transformed output after iteration completes

#### Scenario: Malformed export fails before submitting

- **WHEN** a task declares an `export.as` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted

### Requirement: Task flow directives control the next step

A completed task's `then` SHALL determine the next step in the task's own sequence. `continue`, or the absence of `then`, SHALL advance to the next declared task. A task name SHALL continue execution at that named task in the same sequence; the target MAY be declared before or after the current task, and a name SHALL NOT resolve to a task in a different sequence or scope. `exit` SHALL complete the current sequence; when that sequence is the workflow's top-level sequence, the workflow SHALL conclude. Within a `for` body, `exit` SHALL complete only the current iteration's body and continue with the next element. `end` SHALL conclude the workflow from any depth.

The directive SHALL run after the step's `output.as` and `export.as` effects. It SHALL NOT run for a step skipped by `if` or for a failed step. An `end` directive SHALL conclude the workflow without applying the `output.as` or `export.as` of an enclosing `do`, `for`, or `fork` step that has not completed. Document `output.as` SHALL still shape the resulting final value.

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

#### Scenario: Exit inside a for body completes only the current iteration

- **WHEN** a task inside a `for` body completes with `then: exit`
- **THEN** the remaining tasks in that iteration are skipped, that iteration completes, and the next element begins

#### Scenario: Exit in the top-level sequence concludes the workflow

- **WHEN** a task in the document's top-level `do` completes with `then: exit`
- **THEN** the remaining top-level tasks are skipped and the workflow concludes with the step's transformed output

#### Scenario: End from a nested sequence concludes the workflow

- **WHEN** a task inside a nested `do`, a `for` body, or a fork branch completes with `then: end`
- **THEN** the workflow concludes with that step's transformed output, the remaining steps and branches are abandoned, and no enclosing `do`, `for`, or `fork` applies its own `output.as` or `export.as`

#### Scenario: A skipped step does not run its directive

- **WHEN** a task with `then` is skipped because its `if` condition is false
- **THEN** no flow directive runs, the data value is unchanged, and the next declared task runs

#### Scenario: A failed step does not run its directive

- **WHEN** a task's body, `output.as`, or `export.as` fails
- **THEN** the workflow fails and the task's `then` does not run

### Requirement: Synchronous step execution is bounded

The system SHALL bound the number of steps a sequence advances in one synchronous stretch, because flow directives may jump backward and create a loop that never suspends. When that bound is exceeded, the workflow SHALL fail with `INTERNAL`, naming the step being advanced, instead of continuing to execute. The bound SHALL be implementation-defined and generous enough not to affect ordinary workflows. Accounting SHALL begin again when a sequence resumes after awaiting a child, so a loop that suspends on a child task is not bounded by this rule. A `for` task SHALL likewise bound the number of iterations it advances in one synchronous stretch, and its accounting SHALL begin again when it resumes after awaiting a child.

#### Scenario: A synchronous self-jump fails instead of hanging

- **WHEN** a task declares `then` naming itself and executes no child task
- **THEN** the workflow fails with an `INTERNAL` status after exceeding the synchronous bound, naming the step

#### Scenario: A synchronous cycle across tasks fails

- **WHEN** two or more tasks in one sequence redirect to each other and none awaits a child
- **THEN** the workflow fails with an `INTERNAL` status instead of advancing forever

#### Scenario: A synchronous for over a large collection fails instead of wedging the loop

- **WHEN** a `for` task iterates many elements without any iteration suspending on a child
- **THEN** the workflow fails with an `INTERNAL` status after exceeding the synchronous iteration bound, naming the `for` task

#### Scenario: A for that suspends is not bounded by the synchronous iteration rule

- **WHEN** a `for` body awaits a child on each iteration
- **THEN** the iteration accounting begins again after each resume and the loop is not bounded by this rule

#### Scenario: A bounded synchronous sequence completes normally

- **WHEN** a sequence advances fewer steps than the bound before completing or suspending
- **THEN** it behaves as specified without a guard failure
