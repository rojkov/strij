# message-handler

## Purpose

<!-- TBD -->

## Requirements

### Requirement: MessageHandler abstract interface
The system SHALL define a `MessageHandler` abstract interface with an `OnMessage(std::span<const std::byte> msg, Connection& conn)` method. Implementations SHALL define application-level behavior when a complete protocol message is received.

#### Scenario: Handler processes a message
- **WHEN** `OnMessage` is called with a complete message payload and a connection reference
- **THEN** the handler SHALL execute its application-specific logic (e.g., build a response and write it to the connection)

#### Scenario: Handler writes a response via Connection
- **WHEN** the handler has data to send back to the client
- **THEN** the handler SHALL call `conn.Write(response_bytes)` to initiate the write

### Requirement: HttpEchoHandler implementation
The system SHALL provide an `HttpEchoHandler` implementing `MessageHandler` that formats an HTTP 200 OK response with the received body as plain text, identical to the current echo behavior in `Connection`.

#### Scenario: HttpEchoHandler echoes a request body
- **WHEN** `HttpEchoHandler::OnMessage` is called with an HTTP request body
- **THEN** it SHALL format an HTTP/1.1 200 OK response with `Content-Type: text/plain`, `Connection: close`, and the body as the response payload
- **AND** it SHALL call `conn.Write(response_bytes)` to send the response

#### Scenario: HttpEchoHandler closes connection after response
- **WHEN** the HTTP response has been fully written
- **THEN** the connection SHALL be closed (via the existing end-of-stream lifecycle in Connection)
