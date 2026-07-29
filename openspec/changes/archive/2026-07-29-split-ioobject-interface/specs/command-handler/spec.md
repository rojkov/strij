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
