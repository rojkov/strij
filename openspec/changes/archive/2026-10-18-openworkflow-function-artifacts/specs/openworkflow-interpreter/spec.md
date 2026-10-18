# Spec Delta

## MODIFIED Requirements

### Requirement: The data chain carries one value between steps and is the expression subject

The system SHALL maintain a single data value that flows from step to step, which SHALL be either a JSON value or an opaque byte value. Expressions SHALL receive that data value as their subject.

The system SHALL seed that data value from the workflow's invocation input, which is the task body. An empty body SHALL seed JSON `null`. A non-empty body SHALL seed the data value from its bytes: JSON when they parse as JSON, opaque otherwise. If the document declares `input.from`, the system SHALL evaluate it with the invocation input as its subject and use its result as the seed instead. The subject at a step SHALL be the data value produced by the step before it.

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

#### Scenario: Empty body seeds JSON null

- **WHEN** a workflow is invoked with an empty body and the document declares no `input.from`
- **THEN** the data value is JSON `null`

#### Scenario: Empty body is the subject for document input

- **WHEN** a workflow is invoked with an empty body and the document declares an `input.from` expression
- **THEN** that expression is evaluated with JSON `null` as its subject

#### Scenario: Child result becomes the next subject

- **WHEN** a step completes with a result
- **THEN** that result is the data value, and the following step's expression subject is that result

#### Scenario: JSONness is re-evaluated at every step boundary

- **WHEN** a step produced a JSON data value and the following step's result is not JSON
- **THEN** the data value for the following step is recorded as non-JSON, and a subsequent expression requiring a JSON subject fails

#### Scenario: `$context` is not the data chain

- **WHEN** a step's expression reads `$context`
- **THEN** it observes an empty object, not the data chain's current value

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
