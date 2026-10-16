# Spec Delta

## REMOVED Requirements

### Requirement: Workflow task handler example

**Reason**: The `"workflow"` handler was an example of the submission seam, not a workflow interpreter. It submits a fixed number of identical children and aggregates their final bodies, which does not interpret a workflow document. The real interpreter supersedes it, and its reference-pattern role is served by the `openworkflow` handler.

**Migration**: The submission seam itself is unchanged — `ChildTaskSubmitter`, the per-type router, and the sender-backed `RunTask` overload all keep their contracts. Code or node configuration referencing the `"workflow"` task type SHALL migrate to `"openworkflow"`. Code that used the handler as a submission-seam example SHALL read the `openworkflow` handler instead, whose factory retrieves the submitter at construction by the same contract.

## MODIFIED Requirements

### Requirement: Node-side per-type child scheduler router with explicit local default

The nodeagent SHALL route child-task submissions by `task.type()` through a child scheduler router. Node-side scheduler entries SHALL be able to declare local scheduling authority explicitly and independently: a `task_type` declaration makes the entry authoritative over locally-originated tasks of that type; a `local_default` declaration (at most one entry) makes the entry the node's fallback authority for locally-originated tasks no other entry claims. An entry with neither declaration SHALL schedule no locally-originated tasks. An empty `task_type` SHALL NOT make an entry the default (diverging from the gateway `SchedulerConfig` semantics). The router SHALL be the single submission entry point: `ChildTaskSubmitter::Submit` routes to the router, which delegates `Schedule(child, receiver)` to the entry declaring the child's type, falls back to the `local_default` entry, and delivers an error through the receiver when no entry claims the type and no local default is declared. The router SHALL also be the node's frame demux (`child-result-routing`): its `HandledFrameTypes()` is the union of its constituents' handled types and its `HandleFrame` dispatches by `type_id` to the claiming scheduler.

#### Scenario: Type-claimed child routes to the declaring scheduler

- **WHEN** a scheduler entry declares `task_type = "openworkflow"` and a child of type `"openworkflow"` is submitted
- **THEN** the `Schedule` call SHALL reach that scheduler

#### Scenario: Unclaimed child routes to the explicit local default

- **WHEN** a child of type `"render"` is submitted, no entry claims `"render"`, and exactly one entry declares `local_default = true`
- **THEN** the `Schedule` call SHALL reach the local-default scheduler

#### Scenario: Scheduler without a local role schedules nothing

- **WHEN** a scheduler entry declares neither `task_type` nor `local_default`
- **THEN** it SHALL NOT receive any locally-originated task submission
- **AND** it SHALL continue to handle its wire-protocol frames exactly as before

#### Scenario: Unclaimed child without a local default is rejected

- **WHEN** a child type matches no entry and no entry declares `local_default`
- **THEN** the router SHALL deliver an error to the child's receiver
