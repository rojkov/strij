## ADDED Requirements

### Requirement: TaskProbe message schema

The system SHALL define a Protobuf message `TaskProbe` in package `strij.task` with fields `string id = 1`, `string type = 2`, and `ResourceRequirements requirements = 3`. The `id` SHALL match the originating `Task.id`; the `type` and `requirements` SHALL let the node admit the task without the task body. The message SHALL NOT carry the task body.

#### Scenario: TaskProbe carries id, type, and requirements

- **WHEN** a `TaskProbe` for task "probe_fox_77" with type "echo" and requirements `{"cpu": 2}` is serialized into a TLV frame and parsed back
- **THEN** the parsed `TaskProbe` SHALL expose id="probe_fox_77", type="echo", and requirements `{"cpu": 2}`, with no body field

### Requirement: TaskPull message schema

The system SHALL define a Protobuf message `TaskPull` in package `strij.task` with field `string id = 1`. The `id` SHALL match the originating `Task.id` the node is claiming.

#### Scenario: TaskPull carries the claimed task id

- **WHEN** a node serializes a `TaskPull` for task "probe_fox_77" into a TLV frame and a gateway parses it back
- **THEN** the parsed `TaskPull` SHALL expose id="probe_fox_77"

### Requirement: TaskProbeCancel message schema

The system SHALL define a Protobuf message `TaskProbeCancel` in package `strij.task` with field `string id = 1`. The `id` SHALL match the originating `Task.id` being relinquished; cancellation SHALL be idempotent on the receiving side.

#### Scenario: TaskProbeCancel carries the relinquished task id

- **WHEN** a gateway serializes a `TaskProbeCancel` for task "probe_fox_77" into a TLV frame and a node parses it back
- **THEN** the parsed `TaskProbeCancel` SHALL expose id="probe_fox_77"

### Requirement: TaskDecline message schema

The system SHALL define a Protobuf message `TaskDecline` in package `strij.task` with fields `string id = 1` and `string reason = 2`. The `id` SHALL match the originating `Task.id` being refused; the `reason` SHALL describe why the node refused (e.g. "queue full" or "requirements unsatisfiable").

#### Scenario: TaskDecline carries id and reason

- **WHEN** a node serializes a `TaskDecline` for task "probe_fox_77" with reason "queue full" into a TLV frame and a gateway parses it back
- **THEN** the parsed `TaskDecline` SHALL expose id="probe_fox_77" and reason="queue full"