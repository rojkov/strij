# protocol-parser

## Purpose

<!-- TBD -->

## Requirements

### Requirement: ProtocolParser abstract interface
The system SHALL define a `ProtocolParser` abstract interface that all protocol parsers implement. The interface SHALL provide `GetReadBuffer()` returning a `std::span<std::byte>` writable buffer for io_uring to read into, and `OnData(size_t bytes_read)` to process data written into that buffer. Parsers SHALL return an `Action` enum (`NeedMoreData` or `MessageComplete`) indicating whether they require additional input. Parsers SHALL deliver complete messages via an `OnMessage` callback set at construction time. Parsers SHALL NOT inherit from `event::IOObject`.

#### Scenario: Parser delivers a complete message
- **WHEN** `OnData(N)` is called and the parser has accumulated enough bytes for a complete message
- **THEN** the parser SHALL invoke the `OnMessage` callback with the assembled message payload
- **AND** return `Action::MessageComplete`

#### Scenario: Parser needs more data
- **WHEN** `OnData(N)` is called and the parser has not yet accumulated a complete message
- **THEN** the parser SHALL return `Action::NeedMoreData`

#### Scenario: Parser provides a stable read buffer
- **WHEN** `GetReadBuffer()` is called
- **THEN** the parser SHALL return a writable `std::span<std::byte>` pointing to memory that remains valid until the next `OnData()` call
- **AND** the buffer SHALL be suitable for io_uring to write into directly (zero-copy from kernel to parser)

### Requirement: LlhttpParser implements ProtocolParser
The existing `LlhttpParser` class SHALL be refactored to implement the `ProtocolParser` interface. It SHALL no longer inherit from `event::IOObject`. Its `GetReadBuffer()` SHALL return the writable span from its existing `Chunk` system (the `active_chunk_->WritableSpan()`). Its `OnData(size_t)` SHALL advance the chunk cursor, parse via `llhttp_execute`, and return the appropriate `Action`. The existing HTTP parsing logic (llhttp integration, chunk management, body assembly) SHALL remain unchanged.

#### Scenario: LlhttpParser parses an HTTP request
- **WHEN** `OnData(N)` is called after io_uring wrote a complete HTTP request into `GetReadBuffer()`
- **THEN** `LlhttpParser` SHALL invoke the `OnMessage` callback with the request body
- **AND** return `Action::MessageComplete`

#### Scenario: LlhttpParser handles partial HTTP data
- **WHEN** `OnData(N)` is called with a partial HTTP request
- **THEN** `LlhttpParser` SHALL advance its chunk cursor and return `Action::NeedMoreData`
- **AND** the next `GetReadBuffer()` call SHALL return a span into the remaining writable space of the current chunk (or a new chunk if the current one is full)

#### Scenario: LlhttpParser is not an IOObject
- **WHEN** `LlhttpParser` is constructed
- **THEN** it SHALL NOT inherit from `event::IOObject` and SHALL NOT implement `HandleCompletion` or `ProcessCommand`

#### Scenario: LlhttpParser read buffer is zero-copy
- **WHEN** `GetReadBuffer()` returns a span and io_uring writes N bytes into it
- **THEN** `OnData(N)` SHALL process the data in-place within the parser's own chunk memory without copying
