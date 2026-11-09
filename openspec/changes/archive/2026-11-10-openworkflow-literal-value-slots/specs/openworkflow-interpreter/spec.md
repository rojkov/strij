# Spec Delta

## ADDED Requirements

### Requirement: Expression-only slots accept only whole-value wrapped expressions

For the expression-only slots — a task's `if`, a `switch` case's `when`, a `for` task's `while`, and the string form of a `for` task's `in` — the system SHALL treat only a string wholly wrapped in `${ }` as an expression. A bare string in one of these slots SHALL be rejected as a malformed document rather than treated as literal text. These slots exist to produce a single typed result, so literal text is never a meaningful value in them.

#### Scenario: Bare condition is malformed

- **WHEN** a task declares `if: .enabled`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare switch condition is malformed

- **WHEN** a switch case declares `when: .orderType == "electronic"`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare for collection is malformed

- **WHEN** a `for` task declares `in: .colors` rather than a whole `${ }` expression or an inline array
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Bare while is malformed

- **WHEN** a `for` task declares `while: .vet != null`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Wrapped expression-only slots are accepted

- **WHEN** a task declares `if: '${ .enabled }'`, a switch case declares `when: '${ .orderType == "electronic" }'`, and a `for` task declares `in: '${ .colors }'` and `while: '${ .vet != null }'`
- **THEN** the document parses, validates, and executes those slots as expressions

### Requirement: Value-construction slots accept literal text alongside expressions

The value-construction slots — a task's `input.from`, `output.as`, and `export.as`, the document's `input.from`, and the document's `output.as` — SHALL be literal-capable value trees. A string wholly wrapped in `${ }` SHALL be evaluated, a bare string SHALL stay literal text, a non-string scalar SHALL be used as that value, and arrays and objects SHALL recurse into their members while preserving structure and keys.

#### Scenario: Literal member stays literal

- **WHEN** a task declares `input.from: { content_type: "application/json", mode: "batch" }`
- **THEN** the transformed input is the object with those literal string values

#### Scenario: Literal source text and an expression member mix

- **WHEN** a task declares an `input.from` object whose `python_code` member is a literal multi-line string and whose `input` member is `'${ .value }'`
- **THEN** the transformed input carries the literal source text unchanged and the evaluated value of `.value` as its `input` member

#### Scenario: Literal root string is a JSON string

- **WHEN** a task declares `input.from: hello`
- **THEN** the transformed input is the JSON string `"hello"`, not the opaque bytes `hello`

#### Scenario: Wrapped member is evaluated

- **WHEN** a task declares `input.from: { id: '${ .payload.id }' }`
- **THEN** the `id` member is the evaluated value from the payload

#### Scenario: Partial wrapper is malformed

- **WHEN** a task declares `input.from: { name: 'job-${ .id }' }`
- **THEN** the workflow fails with a malformed-document error and no child task is submitted

#### Scenario: Malformed expression fails before submitting

- **WHEN** a value-construction slot contains a `${ }`-wrapped string that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted

### Requirement: A value-construction slot requires a JSON subject only when it contains an expression

A value-construction slot that contains no expression SHALL be evaluated without a JSON subject, even when the value it would otherwise transform is opaque bytes. A value-construction slot that contains an expression SHALL require its expression subject to be JSON, and SHALL fail the step during execution with a subject error naming the step when it is not.

#### Scenario: Literal-only input transformation over opaque input

- **WHEN** a task whose raw input is opaque bytes declares `input.from: { mode: "batch" }`
- **THEN** the transformed input is that literal object and no subject error occurs

#### Scenario: Literal-only output transformation over opaque output

- **WHEN** a step whose raw output is opaque bytes declares `output.as: { normalized: true }`
- **THEN** the transformation succeeds without a subject error

#### Scenario: Expression over opaque output fails

- **WHEN** a step whose raw output is opaque bytes declares `output.as: '${ .value }'`
- **THEN** the workflow fails during execution with a subject error naming the step

## MODIFIED Requirements

### Requirement: Task output.as transforms the step's raw output

When a completed `call`, `do`, `for`, or `fork` step declares `output.as`, the system SHALL evaluate it against the step's raw output and use its JSON-faithful result as the data value for the following step. The raw output SHALL be the expression subject; `$output` SHALL name that same raw output, and `$input` SHALL name the step's transformed input. When the step declares no `output.as`, its raw output SHALL become the data value. A declared `output.as` that contains an expression SHALL require both the transformed input and the raw output to be JSON; if either is opaque, the step SHALL fail during execution with a subject error naming the step. A declared `output.as` that contains no expression SHALL be evaluated without a JSON subject.

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

- **WHEN** a step declares an `output.as` that contains an expression and its raw output is not JSON
- **THEN** the workflow fails during execution with an error stating the subject is not JSON and naming the step

#### Scenario: Output transformation over opaque input fails

- **WHEN** a step declares an `output.as` that contains an expression and its transformed input is not JSON
- **THEN** the workflow fails during execution with an error stating the input is not JSON and naming the step

#### Scenario: Malformed output transformation fails before submitting

- **WHEN** a task declares an `output.as` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted

### Requirement: Document output.as transforms the workflow's final output

When a workflow completes successfully and the document declares `output.as`, the system SHALL evaluate it against the last transformed data value and use its JSON-faithful result as the final result body. The last data value SHALL be the expression subject, and `$context` SHALL be present with the current workflow context. When the document declares no `output.as`, the last data value SHALL be the final result body. A declared `output.as` that contains an expression over a final value that is not JSON SHALL fail the workflow during execution with a subject error. A declared `output.as` that contains no expression SHALL be evaluated without a JSON subject.

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

- **WHEN** the document declares an `output.as` that contains an expression and the last data value is not JSON
- **THEN** the workflow fails during execution with an error stating the subject is not JSON

#### Scenario: Malformed document output fails before submitting

- **WHEN** the document declares an `output.as` expression that is not a valid program
- **THEN** the workflow fails in the pre-execution validation pass with a malformed-document error and no child task is submitted

### Requirement: Task export.as updates the workflow context

When a task declares `export` with `as`, the system SHALL evaluate `export.as` against the step's transformed task output after the step's output transformation completes and before the next step begins. The transformed output SHALL be the expression subject; `$output` SHALL name that transformed output, `$input` SHALL name the step's transformed input, and `$context` SHALL name the current context. The result SHALL be a JSON object and SHALL replace the workflow context. A task with no `export`, or an `export` with no `as`, SHALL leave the context unchanged.

`export.as` SHALL run only for a step that completed successfully and was not skipped by `if`. A failed or skipped step SHALL NOT export. It SHALL apply to `call`, `do`, `for`, and `fork` steps. A declared `export.as` that contains an expression SHALL require both the transformed input and the transformed output to be JSON; if either is opaque, the step SHALL fail during execution with a subject error naming the step. A declared `export.as` that contains no expression SHALL be evaluated without a JSON subject. A result that is not a JSON object SHALL fail the step during execution.

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

- **WHEN** a task declares an `export.as` that contains an expression and its transformed input or transformed output is not JSON
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

### Requirement: A switch task selects a flow directive from its cases

A `switch` task SHALL evaluate its cases against the transformed task input. Cases that declare `when` SHALL be considered in declaration order, and the first case whose expression yields `true` SHALL select that case. A case without `when` SHALL be a fallback selected only if no conditional case matched. The selected case's `then` SHALL be the step's flow directive. If no case matches, the switch task's own `then` SHALL be used, or `continue` when it is absent. The switch task's raw output SHALL be its transformed task input, and `output.as`, `export.as`, and `then` SHALL apply through the ordinary step pipeline.

A case `when` SHALL be an expression-only slot and SHALL yield a boolean. A non-boolean result SHALL fail the step during execution, naming the task and case. When at least one case declares `when`, a non-JSON transformed input SHALL fail the step during execution with a subject error; a switch with no conditional case SHALL not require a JSON subject. Every case SHALL declare `then`, and a switch SHALL declare at most one case without `when`; violations SHALL be malformed documents rejected before execution.

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

### Requirement: A for task iterates a collection with a carried accumulator

A `for` task SHALL evaluate its `for.in` collection exactly once, against the task's transformed input, before its first iteration, and SHALL execute its `do` body once per element in declaration order. The first iteration SHALL begin with the task's transformed input; each completed iteration's data value SHALL seed the next; and the task's raw output SHALL be the accumulator after the final iteration. When the collection is empty, the raw output SHALL be the transformed input.

`for.in` SHALL accept two forms. A string SHALL be an expression-only slot: a whole-value `${ }` expression evaluated against the transformed input. A non-string SHALL be a literal-capable value tree evaluated against the transformed input: `${ }`-wrapped strings are evaluated, bare strings remain literal, and the resulting array is the collection. The evaluated collection SHALL be an array or `null`; an array is iterated in order, `null` produces zero iterations, and any other result SHALL fail the step during execution with a failure naming the task. An expression over a non-JSON transformed input SHALL fail the step with a subject error.

`for.each` and `for.at` SHALL name the current element and zero-based index. An absent, empty, or whitespace-only `each` defaults to `item`, and an absent, empty, or whitespace-only `at` defaults to `index`; a leading `$` on a declared name is not part of the variable name. The bound variables SHALL be expression bindings scoped to the body: visible to every expression in every task of the body, including nested `do`, `fork`, and `for` bodies, and preserved across a suspension. They SHALL NOT be visible in the `for` task's own `if`, `input.from`, `output.as`, `export.as`, or `then`; `for.in` SHALL observe only loop variables of enclosing loops. A nested `for` that reuses a name SHALL shadow the enclosing binding within its body. The declared names SHALL be valid identifiers and SHALL differ from each other and from the reserved `context`, `input`, and `output` names; a violation SHALL be a malformed document.

After each completed iteration, if the task declares `while`, the system SHALL evaluate `while` against the accumulator, with the current loop variables and `$context` bound, and the result SHALL be a boolean. A false result SHALL stop iteration before the next element and leave the accumulator as the task's raw output. `while` SHALL be an expression-only slot; a non-boolean result, a subject that is not JSON, or a malformed program SHALL fail the step as specified for expression slots. An `end` flow directive inside the body SHALL conclude the workflow immediately and skip the `for` task's own `output.as`, `export.as`, and `then`; an `exit` directive SHALL complete only the current iteration's body.

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

## REMOVED Requirements

### Requirement: Expression-typed slots accept only whole-value wrapped expressions

**Reason**: The uniform rule was designed so that a bare `if: .enabled` could not silently mean "always true". Extending it to the slots whose job is to build a value made their object form unable to carry any constant text, so payloads such as inline source code or a fixed content type could not be expressed at all. The rule is now split: only the slots that must produce a single typed result stay expression-only, and the value-construction slots follow the literal-text rule.

**Migration**: A bare string in a task's `input.from`, `output.as`, `export.as`, the document's `input.from`, or the document's `output.as` is now literal text instead of a malformed-document error. Documents that executed before are unaffected, because every string in those slots had to be a whole-value `${ }` expression. Authors who relied on the error to catch a missing wrapper must write the wrapper explicitly.
