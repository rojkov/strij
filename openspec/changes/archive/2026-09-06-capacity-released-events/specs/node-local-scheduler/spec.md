## ADDED Requirements

### Requirement: Nodeagent scheduler capacity-release observer

A nodeagent local scheduler SHALL be able to register as a capacity-release observer with the shared `AdmissionController` via `AdmissionController::RegisterCapacityObserver(event::CommandHandler*)`. The `AdmissionController` is shared by all local schedulers on the node; on each capacity release it SHALL submit a `CAPACITY_RELEASED` command addressed to every registered observer through the event dispatcher. The notification SHALL be node-global: capacity freed by any task (regardless of which scheduler's protocol carried it) SHALL wake every registered observer, and each observer SHALL re-verify admission itself. The registration is one-way and happens once at scheduler construction; schedulers and the controller are process-lifetime objects, so no unregistration is required.

#### Scenario: Registered observer is woken on capacity release

- **WHEN** a local scheduler has registered itself with `AdmissionController` and a task on the node completes, releasing its admitted capacity
- **THEN** the controller SHALL submit a `CAPACITY_RELEASED` command addressed to that scheduler
- **AND** the scheduler's `ProcessCommand` SHALL be invoked with that command on the event loop

#### Scenario: Release with no registered observers submits no commands

- **WHEN** `AdmissionController::Release()` is called while the observer list is empty (the only configured scheduler is `push`, which does not register)
- **THEN** no `Command` SHALL be submitted

#### Scenario: Capacity release after admission is broadcast, not dropped

- **WHEN** a `Release()` call actually frees admitted capacity (the counters decrease) and one or more observers are registered
- **THEN** each registered observer SHALL receive exactly one `CAPACITY_RELEASED` command

### Requirement: Capacity-released wakeups are pure signals

A `CAPACITY_RELEASED` command SHALL carry `args_ == nullptr`; it SHALL carry no payload describing the released capacity. The receiving scheduler SHALL NOT read `args_`; it SHALL re-derive current capacity from the `AdmissionController` (e.g. `SharedFree()`, `InFlight()`, and its own queue) inside `ProcessCommand`. A release of capacity that does not satisfy any queued probe's requirements SHALL be an idempotent no-op on the receiving side.

#### Scenario: Observer re-queries capacity instead of reading a payload

- **WHEN** a scheduler receives a `CAPACITY_RELEASED` command
- **THEN** it SHALL consult the `AdmissionController` for current capacity before acting
- **AND** it SHALL NOT act on any data carried in `args_`

#### Scenario: Spurious wakeup from unrelated capacity is a no-op

- **WHEN** a scheduler receives `CAPACITY_RELEASED` but the freed capacity does not satisfy the requirements of anything it is waiting on
- **THEN** the scheduler SHALL take no admission or forwarding action