# connection-completable

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

### Requirement: Connection does not own read buffers
`Connection` SHALL NOT own a read buffer as a member field. Read buffers SHALL be provided by the parser via `GetReadBuffer()`. This is safe because the io_uring lifetime contract (buffer valid until `HandleCompletion` returns) is satisfied by the ownership chain: `Connection` → `parser_` (unique_ptr member) → parser's internal buffers.

#### Scenario: Read buffer ownership chain
- **WHEN** `Connection` calls `Dispatcher::PrepareRead(this, ReadTag, fd_, parser_->GetReadBuffer(), 0)`
- **THEN** the buffer SHALL be owned by the parser, not by Connection
- **AND** the buffer SHALL remain valid because `Connection` owns `parser_` via `unique_ptr`, keeping the parser alive throughout the connection lifetime

### Requirement: Connection owns the write buffer
`Connection` SHALL own a `std::deque<std::vector<std::byte>> write_queue_` member field. `Connection::Write(data)` SHALL copy `data` into a new buffer and append it to `write_queue_`; if the queue was empty, Connection SHALL immediately register an async write of the front buffer with the Dispatcher. Each buffer SHALL remain valid until its write completes or is aborted. Concurrent `Write` calls SHALL be allowed; buffers SHALL drain in FIFO order, one async write at a time.

#### Scenario: Handler writes a response
- **WHEN** a handler calls `conn.Write(data)`
- **THEN** Connection SHALL copy `data` into a new buffer, append it to `write_queue_`, and if the queue was empty register an async write with the Dispatcher

#### Scenario: Write buffer lifetime
- **WHEN** Connection submits `Dispatcher::PrepareWrite(this, WriteTag, fd_, front buffer from write_offset_, 0)`
- **THEN** the front buffer SHALL remain valid and `write_offset_` SHALL not be modified until the write `HandleCompletion` returns

#### Scenario: Write queues concurrent writes
- **WHEN** `Connection::Write(data)` is called while a previous write is in-flight
- **THEN** the new buffer SHALL be appended to `write_queue_` and SHALL be written after the in-flight write completes

#### Scenario: Connection handles a partial write of the front buffer
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res > 0 and res < remaining bytes in the front buffer
- **THEN** Connection SHALL advance `write_offset_` by res and resubmit a PrepareWrite for the remaining bytes of the front buffer

#### Scenario: Connection handles a complete buffer and drains the queue
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res >= remaining bytes in the front buffer
- **THEN** Connection SHALL pop the front buffer, reset `write_offset_` to 0
- **AND** if `write_queue_` is non-empty, register an async write for the new front buffer

#### Scenario: Connection handles a write error
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res <= 0
- **THEN** Connection SHALL clear `write_queue_`, reset `write_offset_` to 0, and log the error

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
