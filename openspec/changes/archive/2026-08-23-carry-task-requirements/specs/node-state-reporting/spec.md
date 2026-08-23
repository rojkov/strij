## MODIFIED Requirements

### Requirement: Nodeagent admission control

On each `kTaskSubmission`, the nodeagent SHALL admit the task using the hardware requirements carried on the received `Task.requirements`. A task submitted without the `requirements` field SHALL be treated as declaring empty requirements (unconstrained by pools; per-type concurrency limits still apply). The nodeagent SHALL NOT fall back to operator-declared or handler-declared default resources. The task SHALL be admitted only if the shared free capacity of every requested pool is at least the task's requirement and the task type has concurrency headroom (unlimited when the handler's `concurrency` is `0` or omitted). On admission it SHALL reserve capacity; otherwise it SHALL NOT reserve and SHALL send a `kTaskRejected` frame.

#### Scenario: Task admitted within capacity

- **WHEN** a task carrying `{"gpu.h100": 1}` is submitted to a node whose shared free `gpu.h100` is 1 and whose handler concurrency has headroom
- **THEN** the task SHALL be admitted and dispatched to the task handler

#### Scenario: Task rejected when a pool is exhausted

- **WHEN** a task carrying `{"gpu.h100": 1}` is submitted to a node whose shared free `gpu.h100` is 0
- **THEN** the nodeagent SHALL send `kTaskRejected` for the task id
- **AND** the task SHALL NOT be dispatched to the task handler

#### Scenario: Requirements are taken from the submitted task, not node defaults

- **WHEN** a task carrying `{"cpu": 4}` is submitted to a node whose handler configuration declares no defaults
- **THEN** admission SHALL reserve 4 units of pool `cpu`
- **AND** no other requirement source SHALL influence the decision

#### Scenario: Absent requirements field is treated as empty

- **WHEN** a task without the `requirements` field set is submitted to a node with declared pools
- **THEN** the task SHALL be admitted subject only to the task type's concurrency limit
- **AND** no pool capacity SHALL be reserved for it
