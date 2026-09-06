## ADDED Requirements

### Requirement: CAPACITY_RELEASED command is a pure wakeup

`event::Command::Type` SHALL include `CAPACITY_RELEASED` in addition to `ACTIVATE_READ` and `DEFERRED_DELETE`. A `CAPACITY_RELEASED` command SHALL be addressed to a `CommandHandler*` destination and SHALL carry `args_ == nullptr`; it is a pure signal that capacity may have changed, not a data payload. The receiving `CommandHandler` SHALL inspect `cmd.type_`, SHALL NOT read `args_`, and SHALL re-derive state from its own sources of truth (e.g. re-querying the `AdmissionController`). The dispatcher SHALL deliver it on the event loop like any other command.

#### Scenario: CAPACITY_RELEASED is delivered on the event loop

- **WHEN** a producer submits a `Command` with `type_ = CAPACITY_RELEASED` and a destination
- **THEN** the dispatcher SHALL call `destination->ProcessCommand(cmd)` on the next event loop tick

#### Scenario: CAPACITY_RELEASED carries no args payload

- **WHEN** a `Command` with `type_ = CAPACITY_RELEASED` is constructed
- **THEN** its `args_` SHALL be `nullptr`
- **AND** the receiving handler SHALL NOT dereference or interpret `args_`