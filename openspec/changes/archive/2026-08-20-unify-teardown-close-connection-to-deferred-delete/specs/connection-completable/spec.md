# connection-completable

## Purpose

<!-- TBD -->

## MODIFIED Requirements

### Requirement: Connection is an Completable

`Connection` SHALL implement `event::Completable` and override `HandleCompletion`. It SHALL NOT implement `event::CommandHandler`. It SHALL register itself as the `Completable` for all read and write operations with the Dispatcher. It SHALL hold an `event::CommandHandler*` reference to its owner for submitting lifecycle commands.

#### Scenario: Connection handles a read completion

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Read, res=N)` with N > 0
- **THEN** Connection SHALL call `parser_->OnData(N)` to let the parser process the data in-place
- **AND** if the parser returns `Action::NeedMoreData`, Connection SHALL call `Dispatcher::PrepareRead(this, ReadTag, fd_, parser_->GetReadBuffer(), 0)` to re-arm the read

#### Scenario: Connection handles a read error or EOF

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Read, res<=0)`
- **THEN** Connection SHALL invoke end-of-stream handling (close fd, submit DEFERRED_DELETE)

#### Scenario: Connection handles a write completion

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res > 0 and res < remaining bytes
- **THEN** Connection SHALL advance `write_offset_` by res and resubmit a PrepareWrite for the remaining bytes

#### Scenario: Connection handles a complete write

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res >= remaining bytes in write_buf_
- **THEN** Connection SHALL clear `write_buf_` and reset `write_offset_` to 0

#### Scenario: Connection handles a write error

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res <= 0
- **THEN** Connection SHALL clear `write_buf_`, reset `write_offset_` to 0, and log the error

#### Scenario: Connection sends DEFERRED_DELETE to owner

- **WHEN** end-of-stream is detected
- **THEN** Connection SHALL submit a `Command` with `type_=DEFERRED_DELETE` targeting its owner (`CommandHandler*`)
- **AND** the command payload SHALL contain a `Connection*` pointer to itself
