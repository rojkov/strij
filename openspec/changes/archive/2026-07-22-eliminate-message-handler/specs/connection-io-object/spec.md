## MODIFIED Requirements

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
