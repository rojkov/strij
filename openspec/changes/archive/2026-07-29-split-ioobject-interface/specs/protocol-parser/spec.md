## MODIFIED Requirements

### Requirement: ProtocolParser abstract interface

The system SHALL define a `ProtocolParser` abstract interface that all protocol parsers implement. The interface SHALL provide `GetReadBuffer()` returning a `std::span<std::byte>` writable buffer for io_uring to read into, and `OnData(size_t bytes_read)` to process data written into that buffer. Parsers SHALL return an `Action` enum (`NeedMoreData` or `MessageComplete`) indicating whether they require additional input. Parsers SHALL deliver complete messages via an `OnMessage` callback set at construction time. Parsers SHALL NOT implement `event::Completable`.

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

*(Unchanged — same as original spec)*

### Requirement: LlhttpParser is not an Completable

- **WHEN** `LlhttpParser` is constructed
- **THEN** it SHALL NOT implement `event::Completable` and SHALL NOT implement `HandleCompletion` or `ProcessCommand`
