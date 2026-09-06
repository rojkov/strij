## ADDED Requirements

### Requirement: RunTask with preallocated capacity

The `RunTask` service SHALL provide a variant that runs a task under a caller-supplied admission scope instead of calling `AdmissionController::Admit`. The variant SHALL take the parsed `Task`, the originating `Connection`, and the held `AdmissionScopePtr`; it SHALL look up the task handler by `task.type()` and run it with a result sender that owns the supplied scope (releasing it on the final result). The variant SHALL NOT admit, SHALL NOT emit `kTaskRejected`, and SHALL be the execution path for tasks whose capacity was reserved outside the immediate admission call (e.g. the probe scheduler's preallocation). The existing admitting `RunTask` SHALL remain the push path.

#### Scenario: Preallocated task runs without a second admit

- **WHEN** the probe scheduler invokes the scope variant with a held scope and a granted `Task`
- **THEN** admission counters SHALL increase exactly once (by the held scope) and the handler SHALL run with a result sender owning that scope

#### Scenario: Preallocated task releases on final result

- **WHEN** the granted task's handler emits its final result
- **THEN** the sender SHALL release the supplied scope, triggering the capacity-release notification

### Requirement: Probe scheduler registers as capacity observer

The nodeagent `"probe"` local scheduler SHALL register itself as a capacity-release observer with the shared `AdmissionController` (via `RegisterCapacityObserver`) at construction, so that every capacity release — including those caused by cancel/decline of its own reservations — wakes its queue-walking `ProcessCommand`. It SHALL handle the `CAPACITY_RELEASED` command type; other command types SHALL be ignored. This is the first active consumer of the observer mechanism established for local schedulers.

#### Scenario: Probe scheduler wakes on capacity release

- **WHEN** a task completes on the node and the probe scheduler holds queued probes
- **THEN** the scheduler SHALL be woken by `CAPACITY_RELEASED` and SHALL walk its queue, admitting and pulling what now fits

#### Scenario: Spurious wakeup is a no-op

- **WHEN** the probe scheduler receives `CAPACITY_RELEASED` but no queued probe's requirements are now satisfiable
- **THEN** the scheduler SHALL take no admission or forwarding action