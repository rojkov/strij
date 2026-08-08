## MODIFIED Requirements

### Requirement: LlhttpParser implements ProtocolParser
The existing `LlhttpParser` class SHALL implement the `ProtocolParser` interface. It SHALL NOT implement `event::Completable`. Its `GetReadBuffer()` SHALL return the writable span from its existing `Chunk` system (the `active_chunk_->WritableSpan()`). Its `OnData(size_t)` SHALL advance the chunk cursor, parse via `llhttp_execute`, and return the appropriate `Action`. The parser SHALL capture the request path via the llhttp `on_url` callback, SHALL capture request headers via the llhttp `on_header_field` and `on_header_value` callbacks, and SHALL deliver an `HttpRequest` struct containing the path, headers, and body to its callback. The existing HTTP parsing logic (llhttp integration, chunk management, body assembly) SHALL remain unchanged.

#### Scenario: LlhttpParser parses an HTTP request
- **WHEN** `OnData(N)` is called after io_uring wrote a complete HTTP request into `GetReadBuffer()`
- **THEN** `LlhttpParser` SHALL invoke the callback with an `HttpRequest` containing the request path, headers, and body
- **AND** return `Action::MessageComplete`

#### Scenario: LlhttpParser captures request headers
- **WHEN** a request with header `x-strij-function: /usr/bin/cat` is parsed
- **THEN** the delivered `HttpRequest.headers` SHALL contain the pair ("x-strij-function", "/usr/bin/cat")

#### Scenario: LlhttpParser assembles fragmented header values
- **WHEN** llhttp delivers a header value across multiple `on_header_value` callbacks
- **THEN** the parser SHALL assemble the fragments into a single value in `HttpRequest.headers`

#### Scenario: LlhttpParser captures the request path
- **WHEN** a request with path "/tasks/echo" is parsed
- **THEN** the delivered `HttpRequest.path` SHALL be "/tasks/echo"
- **AND** the delivered `HttpRequest.body` SHALL contain the request body

#### Scenario: LlhttpParser handles partial HTTP data
- **WHEN** `OnData(N)` is called with a partial HTTP request
- **THEN** `LlhttpParser` SHALL advance its chunk cursor and return `Action::NeedMoreData`
- **AND** the next `GetReadBuffer()` call SHALL return a span into the remaining writable space of the current chunk (or a new chunk if the current one is full)

#### Scenario: LlhttpParser is not an Completable
- **WHEN** `LlhttpParser` is constructed
- **THEN** it SHALL NOT implement `event::Completable` and SHALL NOT implement `HandleCompletion` or `ProcessCommand`

#### Scenario: LlhttpParser read buffer is zero-copy
- **WHEN** `GetReadBuffer()` returns a span and io_uring writes N bytes into it
- **THEN** `OnData(N)` SHALL process the data in-place within the parser's own chunk memory without copying
