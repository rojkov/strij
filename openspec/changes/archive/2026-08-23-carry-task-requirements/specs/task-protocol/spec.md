## MODIFIED Requirements

### Requirement: Task message schema
The system SHALL define a Protobuf message `Task` in package `strij.task` with fields `string id = 1`, `string type = 2`, `bytes body = 3`, `map<string,string> parameters = 4`, and `ResourceRequirements requirements = 5`. The `id` SHALL be a human-readable, randomly generated string identifier (e.g., `happy_fox_runs_k7m2x9p4`) used to route results back to the originating HTTP client. The `type` SHALL identify the task handler intended to process the task. The `body` SHALL be the task payload. The `parameters` SHALL carry per-task string key-value metadata (e.g. gateway-forwarded request headers) consumed by task handlers. The `requirements` SHALL carry the resolved hardware requirements of the task keyed by pool name; it SHALL be the authoritative source for admission decisions on the nodeagent and capacity accounting on the gateway.

#### Scenario: Task carries string id, type, and body
- **WHEN** a task with id="happy_fox_runs_k7m2x9p4", type="echo", and body "hello" is serialized
- **THEN** the serialized bytes SHALL parse back into a `Task` with id="happy_fox_runs_k7m2x9p4", type="echo", and body "hello"

#### Scenario: Task carries parameters
- **WHEN** a `Task` with `parameters["function"]` set to "/usr/bin/cat" is serialized
- **THEN** the serialized bytes SHALL parse back into a `Task` with `parameters["function"]` equal to "/usr/bin/cat"

#### Scenario: Task carries resolved requirements round-trip
- **WHEN** a `Task` with `requirements.resources = {"cpu": 2, "gpu.h100": 1}` is serialized into a TLV frame and parsed back
- **THEN** the parsed `Task` SHALL expose `requirements.resources` equal to `{"cpu": 2, "gpu.h100": 1}`

#### Scenario: Task without requirements parses as absent
- **WHEN** a `Task` is serialized without setting `requirements`
- **THEN** the parsed `Task` SHALL have `has_requirements()` false
