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
- **WHEN** a `MessageHandler` calls `conn.Write(data)`
- **THEN** Connection SHALL copy `data` into `write_buf_` and register an async write with the Dispatcher

#### Scenario: Write buffer lifetime
- **WHEN** `Connection` calls `Dispatcher::PrepareWrite(this, WriteTag, fd_, write_buf_, 0)`
- **THEN** `write_buf_` SHALL remain valid and unmodified until the write `HandleCompletion` returns

### Requirement: Connection uses abstract parser and handler
`Connection` SHALL accept a `unique_ptr<ProtocolParser>` and a `unique_ptr<MessageHandler>` in its constructor instead of constructing `LlhttpParser` directly. Connection SHALL NOT depend on `LlhttpParser` or any concrete parser type.

#### Scenario: Connection is constructed with injected dependencies
- **WHEN** a `Connection` is created with a parser and handler
- **THEN** it SHALL own both via `unique_ptr` and delegate parsing to the parser and message handling to the handler

#### Scenario: Connection header has no llhttp dependency
- **WHEN** `connection.hh` is included
- **THEN** it SHALL NOT transitively include any llhttp headers or `LlhttpParser` headers

### Requirement: TcpListener uses parser/handler factory
`TcpListener` SHALL accept a factory callable that produces `(unique_ptr<ProtocolParser>, unique_ptr<MessageHandler>)` pairs for each new connection. A default factory producing `LlhttpParser` + `HttpEchoHandler` SHALL be provided.

#### Scenario: TcpListener creates a connection with factory-provided dependencies
- **WHEN** a new TCP connection is accepted
- **THEN** TcpListener SHALL call the factory to obtain a parser and handler, then construct a `Connection` with them

#### Scenario: Default factory produces HTTP components
- **WHEN** TcpListener is constructed without an explicit factory
- **THEN** it SHALL use a default factory that creates `LlhttpParser` + `HttpEchoHandler` pairs
