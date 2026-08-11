# node-advertisement

## Purpose

Defines the node plane: the per-node capability information a nodeagent advertises to gateways over the task connection, the named-pool resource model, the update-channel and scheduling-protocol seams, and the function-requirement resolution seam.

## Requirements

### Requirement: NodeCapabilities protobuf schema

The system SHALL define a `NodeCapabilities` protobuf message (package `strij.node`) with fields `node_id`, `address`, `repeated ResourcePool pools`, `repeated PoolReservation reservations`, `repeated HandlerCapability handlers`, `repeated UpdateChannel update_channels`, `repeated SchedulingProtocol scheduling_protocols`, and `capability_version`.

#### Scenario: NodeCapabilities round-trips through protobuf

- **WHEN** a `NodeCapabilities` with a `node_id`, an `address`, one pool, one handler, one update channel, and one scheduling protocol is serialized and deserialized
- **THEN** all fields SHALL be preserved

### Requirement: Nodeagent advertises on connect

The nodeagent SHALL send a `kNodeAdvertisement` frame carrying the serialized `NodeCapabilities` as the first frame on every accepted gateway connection, before processing any task submissions on that connection.

#### Scenario: Advertisement is the first frame on connect

- **WHEN** a gateway establishes a connection to a nodeagent
- **THEN** the first TLV frame received by the gateway SHALL be `kNodeAdvertisement` with the nodeagent's capabilities

### Requirement: Named resource pools

A node SHALL announce its schedulable capacity as named `ResourcePool` messages (`name`, `total`), where the unit of `total` is a convention of the pool name (`cpu` → cores, `mem` → bytes, `gpu.h100` → devices). `ResourceRequirements` SHALL be a `map<string, uint64>` keyed by pool name.

#### Scenario: Requirements reference named pools

- **WHEN** a `ResourceRequirements` is built with `resources = {"cpu": 2, "gpu.h100": 1}`
- **THEN** each entry SHALL reference the pool name and requested amount

#### Scenario: New hardware needs no schema change

- **WHEN** a node announces a pool named `"gpu.a100"`
- **THEN** it SHALL be representable with the same `ResourcePool` message without adding a protobuf field

### Requirement: Handler reservations are excluded from shared capacity

The system SHALL represent handler-pinned capacity as `PoolReservation` (`task_type`, `pool`, `amount`). A reservation SHALL be excluded from the shared capacity gateways route on: shared free for a pool SHALL be `total − sum(reservations) − in_use`.

#### Scenario: Reserved pool capacity is not schedulable

- **WHEN** a node announces pool `gpu.h100` with total 2 and a reservation `{task_type: "video-encode", pool: "gpu.h100", amount: 1}`
- **THEN** the shared free capacity for `gpu.h100` SHALL be 1 before any task is admitted

### Requirement: Handler capabilities

The system SHALL represent per-type handler capability as `HandlerCapability` with `task_type`, `concurrency` (max concurrent tasks of the type; `0` or omitted SHALL mean no concurrency limit on the node), `function_sourced` (whether the handler consumes function IDs resolved from the function plane), and optional `default_resources` (a `ResourceRequirements` used when the handler is not function-sourced).

#### Scenario: Zero concurrency means no limit

- **WHEN** a `HandlerCapability` is built with `task_type = "echo"` and `concurrency` unset or `0`
- **THEN** the node SHALL impose no concurrency limit on tasks of type `"echo"`

#### Scenario: Handler announces a concurrency limit

- **WHEN** a `HandlerCapability` is built with `task_type = "echo"` and `concurrency = 1024`
- **THEN** the gateway SHALL read the supported task type and its concurrency limit from the advertisement

### Requirement: update_channels seam

The `NodeCapabilities.update_channels` field SHALL list the channels the nodeagent will use to deliver node state. v1 SHALL support exactly one channel kind, `heartbeat` (state over the established task connection), which SHALL be the default and requires no extra gateway machinery.

#### Scenario: Nodeagent advertises the heartbeat channel

- **WHEN** a nodeagent builds its advertisement with `update_channels = [heartbeat]`
- **THEN** the gateway SHALL treat the task connection as the state channel

### Requirement: scheduling_protocols seam

The `NodeCapabilities.scheduling_protocols` field SHALL list the task-flow protocols the nodeagent's admission speaks. v1 SHALL support exactly one protocol, `push` (accept `kTaskSubmission` and run admission). Nodes SHALL advertise `push`; a node that does not advertise a scheduler's required protocol SHALL be excluded from that scheduler's candidate set.

#### Scenario: Nodeagent advertises the push protocol

- **WHEN** a nodeagent builds its advertisement with `scheduling_protocols = [push]`
- **THEN** the gateway SHALL treat the node as eligible for push-based schedulers

#### Scenario: Node lacking a required protocol is excluded

- **WHEN** a scheduler declares a required protocol not present in a node's `scheduling_protocols`
- **THEN** the gateway SHALL exclude that node from the scheduler's candidate set

### Requirement: Gateway stores and validates the advertisement

The gateway SHALL store the received `NodeCapabilities` on the owning `Node` and expose it to schedulers. When `capability_version` differs from the gateway's supported version, the gateway SHALL log a warning and continue (v1).

#### Scenario: Capability version mismatch warns

- **WHEN** a node advertises a `capability_version` different from the gateway's supported version
- **THEN** the gateway SHALL log a warning
- **AND** the node SHALL remain usable

### Requirement: RequirementsResolver seam

The system SHALL define a `RequirementsResolver` interface that resolves the hardware requirements for a `FunctionRef` (`task_type`, `function` id) plus task parameters into a `ResourceRequirements`. The v1 implementation `ParamsOnlyRequirementsResolver` SHALL read resource entries from task parameters and return an empty map when none are present.

#### Scenario: ParamsOnly resolves declared requirements

- **WHEN** `ParamsOnlyRequirementsResolver::Resolve` is called with a `FunctionRef` and task parameters declaring `cpu` and `gpu.h100` amounts
- **THEN** the returned `ResourceRequirements` SHALL contain those amounts

#### Scenario: ParamsOnly returns empty requirements by default

- **WHEN** `ParamsOnlyRequirementsResolver::Resolve` is called with task parameters that declare no resources
- **THEN** the returned `ResourceRequirements` SHALL be empty
