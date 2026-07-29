# connection-io-object

## Purpose

<!-- TBD -->

## Requirements

### Requirement: Connection is an Completable

`Connection` SHALL implement `event::Completable` and override `HandleCompletion`. It SHALL NOT implement `event::CommandHandler`. It SHALL register itself as the `Completable` for all read and write operations with the Dispatcher. It SHALL hold an `event::CommandHandler*` reference to its owner for submitting lifecycle commands.

#### Scenario: Connection handles a read completion

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Read, res=N)` with N > 0
- **THEN** Connection SHALL call `parser_->OnData(N)` to let the parser process the data in-place
- **AND** if the parser returns `Action::NeedMoreData`, Connection SHALL call `Dispatcher::PrepareRead(this, ReadTag, fd_, parser_->GetReadBuffer(), 0)` to re-arm the read

#### Scenario: Connection handles a read error or EOF

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Read, res<=0)`
- **THEN** Connection SHALL invoke end-of-stream handling (close fd, submit CLOSE_CONNECTION)

#### Scenario: Connection handles a write completion

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res > 0 and res < remaining bytes
- **THEN** Connection SHALL advance `write_offset_` by res and resubmit a PrepareWrite for the remaining bytes

#### Scenario: Connection handles a complete write

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res >= remaining bytes in write_buf_
- **THEN** Connection SHALL clear `write_buf_` and reset `write_offset_` to 0

#### Scenario: Connection handles a write error

- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res <= 0
- **THEN** Connection SHALL clear `write_buf_`, reset `write_offset_` to 0, and log the error

#### Scenario: Connection sends CLOSE_CONNECTION to owner

- **WHEN** end-of-stream is detected
- **THEN** Connection SHALL submit a `Command` with `type_=CLOSE_CONNECTION` targeting its owner (`CommandHandler*`)
- **AND** the command payload SHALL contain a `Connection*` pointer to itself

### Requirement: Connection does not own read buffers
`Connection` SHALL NOT own a read buffer as a member field. Read buffers SHALL be provided by the parser via `GetReadBuffer()`. This is safe because the io_uring lifetime contract (buffer valid until `HandleCompletion` returns) is satisfied by the ownership chain: `Connection` → `parser_` (unique_ptr member) → parser's internal buffers.

#### Scenario: Read buffer ownership chain
- **WHEN** `Connection` calls `Dispatcher::PrepareRead(this, ReadTag, fd_, parser_->GetReadBuffer(), 0)`
- **THEN** the buffer SHALL be owned by the parser, not by Connection
- **AND** the buffer SHALL remain valid because `Connection` owns `parser_` via `unique_ptr`, keeping the parser alive throughout the connection lifetime

### Requirement: Connection owns the write buffer
`Connection` SHALL own a `std::vector<std::byte> write_buf_` member field and a `size_t write_offset_` member field. `Connection::Write(data)` SHALL copy `data` into `write_buf_`, set `write_offset_` to 0, and register an async write with the Dispatcher. The buffer SHALL remain valid until the write completes or is aborted.

#### Scenario: Handler writes a response
- **WHEN** a handler calls `conn.Write(data)`
- **THEN** Connection SHALL copy `data` into `write_buf_`, reset `write_offset_` to 0, and register an async write with the Dispatcher

#### Scenario: Write buffer lifetime
- **WHEN** `Connection` calls `Dispatcher::PrepareWrite(this, WriteTag, fd_, remaining bytes from write_offset_, 0)`
- **THEN** `write_buf_` SHALL remain valid and `write_offset_` SHALL not be modified until the write `HandleCompletion` returns

#### Scenario: Write rejects concurrent writes
- **WHEN** `Connection::Write(data)` is called while `write_buf_` is not empty (previous write in-flight)
- **THEN** Connection SHALL assert or otherwise signal a programming error

### Requirement: Connection uses abstract parser and handler
`Connection` SHALL accept a `unique_ptr<ProtocolParser>` in its constructor. Connection SHALL own only the parser. Handler ownership SHALL be managed by the parser's callback lambda (captured as `unique_ptr`). Connection SHALL NOT depend on `LlhttpParser`, any concrete parser type, or any handler type.

#### Scenario: Connection is constructed with injected parser
- **WHEN** a `Connection` is created with a parser
- **THEN** it SHALL own the parser via `unique_ptr` and delegate parsing to the parser
- **AND** the handler SHALL be owned by the parser's callback lambda, not by Connection

#### Scenario: Connection header has no llhttp dependency
- **WHEN** `connection.hh` is included
- **THEN** it SHALL NOT transitively include any llhttp headers or `LlhttpParser` headers

#### Scenario: Connection header has no handler dependency
- **WHEN** `connection.hh` is included
- **THEN** it SHALL NOT transitively include any `MessageHandler` headers

### Requirement: TcpListener uses parser factory
`TcpListener` SHALL accept a factory callable that produces `unique_ptr<ProtocolParser>` for each new connection.

#### Scenario: TcpListener creates a connection with factory-provided parser
- **WHEN** a new TCP connection is accepted
- **THEN** TcpListener SHALL call the factory to obtain a parser, then construct a `Connection` with it

### Requirement: ConnectionFactory type
`ConnectionFactory` SHALL be a `std::function` that takes `Connection&` and returns `std::unique_ptr<ProtocolParser>`. Handler ownership SHALL be internal to the parser's callback (captured as `unique_ptr` in the lambda).

#### Scenario: Factory produces parser with embedded handler
- **WHEN** `ConnectionFactory` is invoked for a new connection
- **THEN** it SHALL return a `unique_ptr<ProtocolParser>` whose callback owns the handler via `unique_ptr` capture
- **AND** the parser SHALL be the sole return value (no pair, no separate handler)
