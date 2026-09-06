# command-handler

## Purpose

Defines the `CommandHandler` interface for inter-object asynchronous messaging via the Dispatcher. Any object that needs to receive commands implements this interface. Objects that never receive commands (e.g., pure I/O handlers) do not.

## Requirements

### Requirement: CommandHandler is a pure virtual interface

`event::CommandHandler` SHALL be an abstract base class with a single pure virtual method `ProcessCommand(event::Command cmd)`. It SHALL be non-copyable, non-movable. Its destructor SHALL be virtual.

#### Scenario: CommandHandler receives a command

- **WHEN** `ProcessCommand(cmd)` is called on an `CommandHandler`
- **THEN** the handler SHALL inspect `cmd.type_` and react accordingly
- **AND** the handler SHALL NOT block or throw

### Requirement: Command destination targets CommandHandler

`event::Command::destination_` SHALL be of type `event::CommandHandler*`.

#### Scenario: Command targets an CommandHandler

- **WHEN** a `Command` is submitted via `Dispatcher::SubmitCommand(Command{type, destination, args})`
- **THEN** the Dispatcher SHALL deliver the command by calling `destination->ProcessCommand(cmd)` on the next event loop tick

### Requirement: CAPACITY_RELEASED command is a pure wakeup

`event::Command::Type` SHALL include `CAPACITY_RELEASED` in addition to `ACTIVATE_READ` and `DEFERRED_DELETE`. A `CAPACITY_RELEASED` command SHALL be addressed to a `CommandHandler*` destination and SHALL carry `args_ == nullptr`; it is a pure signal that capacity may have changed, not a data payload. The receiving `CommandHandler` SHALL inspect `cmd.type_`, SHALL NOT read `args_`, and SHALL re-derive state from its own sources of truth (e.g. re-querying the `AdmissionController`). The dispatcher SHALL deliver it on the event loop like any other command.

#### Scenario: CAPACITY_RELEASED is delivered on the event loop

- **WHEN** a producer submits a `Command` with `type_ = CAPACITY_RELEASED` and a destination
- **THEN** the dispatcher SHALL call `destination->ProcessCommand(cmd)` on the next event loop tick

#### Scenario: CAPACITY_RELEASED carries no args payload

- **WHEN** a `Command` with `type_ = CAPACITY_RELEASED` is constructed
- **THEN** its `args_` SHALL be `nullptr`
- **AND** the receiving handler SHALL NOT dereference or interpret `args_`
