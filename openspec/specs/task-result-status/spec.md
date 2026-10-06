# task-result-status

## Purpose

Defines the failure classification carried on a task result: a status field whose codes describe what went wrong, the rule that only a task's first result may declare failure, and the total mapping from a code to an HTTP status.

## Requirements

### Requirement: Task results carry a failure status

A task result SHALL carry a status field of enum type. The enum SHALL be declared alongside the task protocol's messages rather than inline in a consumer's namespace, so that the codes and their names exist in one place. The field SHALL be optional, and its absence SHALL mean success. The enum's zero member SHALL mean success. Any other value SHALL mean failure, and in that case the result's body SHALL be interpreted as an error message rather than a result payload.

The component that sends a result it knows to be a failure SHALL set a non-OK status; one that cannot classify the failure specifically SHALL still set a non-OK status, so that a failure is never reported as a success.

#### Scenario: Absent status means success

- **WHEN** a result is produced without setting the status field
- **THEN** the consumer SHALL read that result as a success

#### Scenario: The enum's zero member means success

- **WHEN** a result is produced with a status of the enum's zero member
- **THEN** the consumer SHALL read that result as a success

#### Scenario: Any other member means failure

- **WHEN** a result is produced with a status that is not the enum's zero member
- **THEN** the consumer SHALL read that result as a failure
- **AND** SHALL interpret its body as an error message

#### Scenario: An unnamed member still means failure

- **WHEN** a result carries a status value that has no member in the consumer's schema
- **THEN** the consumer SHALL read that result as a failure
- **AND** SHALL interpret its body as an error message

#### Scenario: A sender with no specific classification still fails

- **WHEN** a failure's classification does not correspond to a declared code beyond "an internal failure"
- **THEN** the sender SHALL set the internal-failure status rather than leaving the field unset
- **AND** the consumer SHALL read the result as a failure rather than a success

### Requirement: Declared status codes describe the failure

The system SHALL declare a fixed, small set of status codes, each with a defined meaning and a defined HTTP counterpart. The set SHALL cover at least: a malformed or unusable request, a request naming something that does not exist, a request for a capability that is not implemented, a task that timed out or exceeded its deadline, a request refused for capacity reasons, a task that failed for an internal reason, and a temporary inability to serve the request.

The set SHALL be small enough that each code's meaning is unambiguous, and SHALL be declared as protobuf enum members so that a consumer can refer to a code by name rather than by number.

A protobuf enum is open: a newer producer may send a member the consumer's schema does not name. The set SHALL be designed so that this is safe — a consumer reading an unrecognized value SHALL treat it as a failure and SHALL map it to the HTTP status designated for an internal failure, never to success.

#### Scenario: Malformed request has its own code

- **WHEN** a task fails because its request was malformed or unusable
- **THEN** the result SHALL carry the code designated for a malformed request

#### Scenario: Missing resource has its own code

- **WHEN** a task fails because the thing it named does not exist
- **THEN** the result SHALL carry the code designated for a missing resource

#### Scenario: Unimplemented capability has its own code

- **WHEN** a task fails because the requested capability is recognized but not implemented
- **THEN** the result SHALL carry the code designated for an unimplemented capability

#### Scenario: Timeout has its own code

- **WHEN** a task fails because it exceeded its deadline
- **THEN** the result SHALL carry the code designated for a timeout

#### Scenario: Capacity refusal has its own code

- **WHEN** a task is refused because no capacity was available
- **THEN** the result SHALL carry the code designated for a capacity refusal

#### Scenario: Internal failure has its own code

- **WHEN** a task fails for a reason with no more specific classification
- **THEN** the result SHALL carry the code designated for an internal failure

#### Scenario: Temporarily unable to serve has its own code

- **WHEN** a request cannot be served at this time but would be expected to succeed later
- **THEN** the result SHALL carry the code designated for temporary unavailability

#### Scenario: An unrecognized code reads as a generic internal failure

- **WHEN** a consumer reads a status it does not recognize
- **THEN** it SHALL read the result as a failure
- **AND** SHALL map it to the HTTP status designated for an internal failure

### Requirement: Every status code maps to an HTTP status

Each declared status code SHALL have exactly one corresponding HTTP status code. The mapping SHALL be total, so that a consumer can always produce an HTTP response for any status it reads.

#### Scenario: Each code has a distinct HTTP counterpart

- **WHEN** the status-to-HTTP mapping is enumerated
- **THEN** every declared code SHALL have a defined HTTP status

#### Scenario: A malformed request maps to a client error

- **WHEN** a result carries the code for a malformed request
- **THEN** the HTTP status SHALL be in the 4xx range

#### Scenario: A capacity refusal maps to 429

- **WHEN** a result carries the code for a capacity refusal
- **THEN** the HTTP status SHALL be 429, since the request was well-formed and the refusal reflects demand rather than a fault

#### Scenario: A temporary unavailability maps to a retryable status

- **WHEN** a result carries the code for temporary unavailability
- **THEN** the HTTP status SHALL be 503

#### Scenario: An internal failure maps to a server error

- **WHEN** a result carries the code for an internal failure
- **THEN** the HTTP status SHALL be in the 5xx range

### Requirement: Only the first result of a task may declare failure

A failure status SHALL be honored only when it appears on the first result of a task. A non-OK status appearing on any later result of the same task SHALL be ignored, because the response's status has already been fixed, and SHALL be logged as unexpected.

#### Scenario: Failure on the first result is honored

- **WHEN** the first result of a task carries a non-OK status
- **THEN** the consumer SHALL report the failure

#### Scenario: Failure on a later result is ignored

- **WHEN** a task has already delivered one or more results and a later result carries a non-OK status
- **THEN** the consumer SHALL NOT change the outcome it has already reported

#### Scenario: Ignored later failure is logged

- **WHEN** a non-OK status is ignored on a later result
- **THEN** the consumer SHALL log it as unexpected, naming the task and the code

#### Scenario: Success status on a later result is unaffected

- **WHEN** a later result of a task carries an OK or absent status
- **THEN** the consumer SHALL treat it as an ordinary result

#### Scenario: A failure followed by more results does not resume the task

- **WHEN** a task's first result carries a non-OK status and further results arrive for that task
- **THEN** the consumer SHALL remain in its failed state and SHALL NOT deliver the further results as a continuation
