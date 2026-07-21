# connection-io-object

## Purpose

<!-- TBD -->

## Requirements

### Requirement: Connection is an IOObject
`Connection` SHALL inherit from `event::IOObject` and implement `HandleCompletion` and `ProcessCommand`. It SHALL register itself as the IOObject for all read and write operations with the Dispatcher.

#### Scenario: Connection handles a read completion
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Read, res=N)` with N > 0
- **THEN** Connection SHALL call `parser_->OnData(N)` to let the parser process the data in-place
- **AND** if the parser returns `Action::NeedMoreData`, Connection SHALL call `Dispatcher::PrepareRead(this, ReadTag, fd_, parser_->GetReadBuffer(), 0)` to re-arm the read

#### Scenario: Connection handles a read error or EOF
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Read, res<=0)`
- **THEN** Connection SHALL invoke end-of-stream handling (close fd, submit CLOSE_CONNECTION)

#### Scenario: Connection handles a write completion
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)`
- **THEN** Connection SHALL invoke end-of-stream handling (close fd, submit CLOSE_CONNECTION)

### Requirement: Connection does not own read buffers
`Connection` SHALL NOT own a read buffer as a member field. Read buffers SHALL be provided by the parser via `GetReadBuffer()`. This is safe because the io_uring lifetime contract (buffer valid until `HandleCompletion` returns) is satisfied by the ownership chain: `Connection` → `parser_` (unique_ptr member) → parser's internal buffers.

#### Scenario: Read buffer ownership chain
- **WHEN** `Connection` calls `Dispatcher::PrepareRead(this, ReadTag, fd_, parser_->GetReadBuffer(), 0)`
- **THEN** the buffer SHALL be owned by the parser, not by Connection
- **AND** the buffer SHALL remain valid because `Connection` owns `parser_` via `unique_ptr`, keeping the parser alive throughout the connection lifetime

### Requirement: Connection owns the write buffer
`Connection` SHALL own a `std::string write_buf_` member field. `Connection::Write(data)` SHALL copy `data` into `write_buf_` before calling `Dispatcher::PrepareWrite(this, WriteTag, fd_, write_buf_, 0)`. This ensures the write buffer is stable even if the caller's data is a temporary.

#### Scenario: Handler writes a response
- **WHEN** a handler calls `conn.Write(data)`
- **THEN** Connection SHALL copy `data` into `write_buf_` and register an async write with the Dispatcher

#### Scenario: Write buffer lifetime
- **WHEN** `Connection` calls `Dispatcher::PrepareWrite(this, WriteTag, fd_, write_buf_, 0)`
- **THEN** `write_buf_` SHALL remain valid and unmodified until the write `HandleCompletion` returns

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
