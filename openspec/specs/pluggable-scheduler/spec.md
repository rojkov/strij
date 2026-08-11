# pluggable-scheduler

## Purpose

Defines the gateway-side `Scheduler` extension category: how scheduling policies are registered, configured, and selected, how candidates are filtered by `scheduling_protocols`, and the v1 push-based policies.

## Requirements

### Requirement: Scheduler extension interface

The system SHALL define a `Scheduler` interface with `RequiredProtocol() -> std::string_view` and `Choose(NodeDirectory&, const TaskOffer&) -> Node*`, where `TaskOffer` bundles a `Task` with its resolved `ResourceRequirements`. A `SchedulerFactory` SHALL be registered in `Registry<SchedulerFactory>` via the existing `REGISTER_FACTORY` macros, following the `ExtensionConfig` configuration pattern.

#### Scenario: Scheduler factory is registered and created

- **WHEN** a `SchedulerFactory` with name `"capability_aware"` is registered and looked up in `Registry<SchedulerFactory>`
- **THEN** the factory SHALL be found
- **AND** `Create(config, context)` SHALL return a `std::unique_ptr<Scheduler>`

#### Scenario: Choose returns a node

- **WHEN** a scheduler's `Choose(dir, offer)` is called with a directory containing an eligible node
- **THEN** it SHALL return a pointer to a node that advertises `RequiredProtocol()`
- **AND** when no eligible node exists, it SHALL return `nullptr`

### Requirement: Candidate filtering by scheduling_protocols

The gateway SHALL restrict a scheduler's candidates to connected nodes whose advertisement lists the scheduler's `RequiredProtocol()` in `scheduling_protocols`.

#### Scenario: Node without required protocol is excluded

- **WHEN** a scheduler requires protocol `"push"` and a node's `scheduling_protocols` does not contain `"push"`
- **THEN** the node SHALL NOT be returned by the scheduler's `Choose`

### Requirement: round_robin policy

The system SHALL provide a `round_robin` scheduler registered as `"round_robin"` that preserves the pre-existing behavior: it SHALL select available (connected) nodes in rotation, advancing the selection index on each call, and SHALL return `nullptr` when no node is available.

#### Scenario: Round-robin rotation over available nodes

- **WHEN** `Choose` is called repeatedly with two available nodes `A` and `B`
- **THEN** the returned nodes SHALL alternate `A, B, A, B, ...`

#### Scenario: Round-robin returns null when none available

- **WHEN** `Choose` is called with no available nodes
- **THEN** it SHALL return `nullptr`

### Requirement: capability_aware policy

The system SHALL provide a `capability_aware` scheduler registered as `"capability_aware"`. It SHALL exclude nodes whose shared free pool capacity is below the offer's `ResourceRequirements` or whose per-type concurrency for the task type is exhausted, and SHALL choose the least-loaded node (lowest free-slot ratio, then lowest in-flight count) among the eligible.

#### Scenario: Node without required pool capacity is excluded

- **WHEN** an offer requires `{"gpu.h100": 1}` and a node's shared free `gpu.h100` is 0
- **THEN** the node SHALL NOT be chosen

#### Scenario: Least-loaded node is chosen

- **WHEN** two nodes both satisfy an offer's requirements, node `A` with in_flight 2 and node `B` with in_flight 5
- **THEN** the scheduler SHALL choose `A`

### Requirement: Scheduler configuration

`GatewayConfig` SHALL expose an `ExtensionConfig scheduler` field. The field SHALL be required: when unset, the gateway SHALL fail to start with an error indicating the scheduler must be configured. When set, the gateway SHALL look up the named factory in `Registry<SchedulerFactory>` and, if the factory is not found, fail to start with an error naming the missing scheduler.

#### Scenario: Missing scheduler fails startup

- **WHEN** a `GatewayConfig` has no `scheduler` field
- **THEN** the gateway SHALL log an error and exit with status 1

#### Scenario: Unknown scheduler name fails startup

- **WHEN** a `GatewayConfig` sets `scheduler.name = "nonexistent"` and no such factory is registered
- **THEN** the gateway SHALL log an error and exit with status 1

### Requirement: GatewayHttpHandler uses the scheduler

The gateway HTTP path SHALL select the target node via the configured scheduler's `Choose`. When `Choose` returns `nullptr`, the gateway SHALL return an error status to the HTTP client (no node available).

#### Scenario: Task routed through scheduler

- **WHEN** an HTTP task request is received
- **THEN** the gateway SHALL resolve the task's requirements, call the configured scheduler's `Choose`, and submit the task to the returned node
