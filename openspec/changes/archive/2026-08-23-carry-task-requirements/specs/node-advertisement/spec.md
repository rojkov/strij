## MODIFIED Requirements

### Requirement: Handler capabilities

The system SHALL represent per-type handler capability as `HandlerCapability` with `task_type`, `concurrency` (max concurrent tasks of the type; `0` or omitted SHALL mean no concurrency limit on the node), and `function_sourced` (whether the handler consumes function IDs resolved from the function plane). `HandlerCapability` SHALL NOT carry resource requirements: hardware requirements are resolved once at the gateway and carried on the submitted `Task`.

#### Scenario: Zero concurrency means no limit

- **WHEN** a `HandlerCapability` is built with `task_type = "echo"` and `concurrency` unset or `0`
- **THEN** the node SHALL impose no concurrency limit on tasks of type `"echo"`

#### Scenario: Handler announces a concurrency limit

- **WHEN** a `HandlerCapability` is built with `task_type = "echo"` and `concurrency = 1024`
- **THEN** the gateway SHALL read the supported task type and its concurrency limit from the advertisement
