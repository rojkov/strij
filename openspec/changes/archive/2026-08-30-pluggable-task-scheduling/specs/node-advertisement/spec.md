## MODIFIED Requirements

### Requirement: scheduling_protocols seam

The `NodeCapabilities.scheduling_protocols` field SHALL list the task-flow protocols the nodeagent speaks, built as the union over the node's configured local schedulers' `RequiredProtocol()`. Each configured local scheduler SHALL contribute its `RequiredProtocol()` to the advertised list. A node that does not advertise a scheduler's required protocol SHALL be excluded from that scheduler's candidate set.

#### Scenario: Nodeagent advertises the push protocol

- **WHEN** a nodeagent configures a local scheduler whose `RequiredProtocol()` is `"push"`
- **THEN** its advertisement SHALL list `"push"` in `scheduling_protocols`
- **AND** the gateway SHALL treat the node as eligible for push-based schedulers

#### Scenario: Nodeagent advertises the union of local scheduler protocols

- **WHEN** a nodeagent configures local schedulers whose `RequiredProtocol()` values are `"push"` and `"probe"`
- **THEN** its advertisement SHALL list both `"push"` and `"probe"` in `scheduling_protocols`

#### Scenario: Node lacking a required protocol is excluded

- **WHEN** a scheduler declares a required protocol not present in a node's `scheduling_protocols`
- **THEN** the gateway SHALL exclude that node from the scheduler's candidate set
