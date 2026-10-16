# Spec Delta

## Purpose

Defines how a nodeagent executes a workflow document: the resumable step loop that runs a workflow task body, the data chain that carries values between steps as the expression subject, the policy for evaluating expressions against it, the split between validation that is possible without data and validation that is not, and how the workflow's own success and failure are reported.

## ADDED Requirements

### Requirement: A workflow document is executed as a resumable step sequence

The system SHALL execute a workflow document's top-level `do` body as an ordered sequence of steps, advancing one step at a time. A step that must await a child's result SHALL suspend the sequence and the workflow SHALL resume at the following step once that result arrives. The system SHALL NOT require the whole body to complete within the call that started the workflow.

#### Scenario: Body runs in declaration order

- **WHEN** a workflow body declares three steps in order and each completes without suspending on external work
- **THEN** the steps execute in declaration order and the workflow completes after the third

#### Scenario: Execution suspends across a child result

- **WHEN** a step submits a child task and suspends awaiting its result
- **THEN** no further step begins until that result arrives, and execution resumes at the step following the suspended one

#### Scenario: Local child completing during submission does not deadlock

- **WHEN** a step's child task is admitted locally and its result is delivered before the submission call returns
- **THEN** the step records that result and execution continues without waiting for a second delivery

### Requirement: The data chain carries one value between steps and is the expression subject

The system SHALL maintain a single data value that flows from step to step, which SHALL be either a JSON value or an opaque byte value. Expressions SHALL receive that data value as their subject. The system SHALL NOT seed the subject from any other value: the subject at a step is the data value produced by the step before it.

The DSL's `$context` SHALL NOT be confused with this subject. `$context` is a separate workflow-scope variable, seeded empty and written only by an `export` or `set` task, neither of which this capability executes. `$context` SHALL therefore be present and empty throughout this capability's execution. A document that reads `$context` in this capability SHALL observe an empty object, and SHALL NOT observe the data chain.

#### Scenario: Data chain seeds the expression subject

- **WHEN** a workflow's input produces a JSON object
- **THEN** the data value is that object and the first step's expression subject is that same object

#### Scenario: Opaque data yields a non-JSON subject

- **WHEN** a workflow's input produces a non-JSON value
- **THEN** the data value is that non-JSON value and an expression requiring a JSON subject fails rather than reading an empty value

#### Scenario: Child result becomes the next subject

- **WHEN** a step completes with a result
- **THEN** that result is the data value, and the following step's expression subject is that result

#### Scenario: JSONness is re-evaluated at every step boundary

- **WHEN** a step produced a JSON data value and the following step's result is not JSON
- **THEN** the data value for the following step is recorded as non-JSON, and a subsequent expression requiring a JSON subject fails

#### Scenario: `$context` is not the data chain

- **WHEN** a step's expression reads `$context`
- **THEN** it observes an empty object, not the data chain's current value

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

The system SHALL perform a validation pass over the document before executing its first step. That pass SHALL cover the facts derivable from the document alone: the call target of each call, the key set of each call's arguments, the value kind of each declared requirement, and the syntax of every runtime expression. A failure in this pass SHALL be reported before any child task is submitted. Failures that depend on data SHALL instead occur during execution.

#### Scenario: Malformed document fails before submitting anything

- **WHEN** a document declares an unknown call target, an unknown key in a call's arguments, a non-integer requirement, or an invalid expression
- **THEN** the workflow fails with that error and no child task has been submitted

#### Scenario: Data-dependent failure occurs during execution

- **WHEN** a document is structurally valid but a step's expression is given a non-JSON subject at run time
- **THEN** the failure occurs during execution rather than in the pre-execution pass

#### Scenario: Validation errors are distinguishable from run errors

- **WHEN** either kind of failure occurs
- **THEN** the reported error indicates whether the document is malformed or the run was given the wrong data

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

- **WHEN** a workflow fails because its body did not parse or failed pre-execution validation, because it requested a recognized-but-unsupported call target, or because a step failed during execution
- **THEN** each case SHALL carry the corresponding distinct status code rather than a single shared failure code

#### Scenario: Failure after a suspension still reports once

- **WHEN** a step suspended awaiting a child and the child is refused
- **THEN** the workflow reports the failure exactly once and does not wait indefinitely for the refused child

#### Scenario: Originating connection closes mid-run

- **WHEN** the originating connection is torn down while the workflow is suspended
- **THEN** the workflow stops without delivering a further result, and children still outstanding are abandoned
