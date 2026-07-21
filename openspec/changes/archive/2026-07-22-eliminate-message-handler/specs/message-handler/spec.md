## REMOVED Requirements

### Requirement: MessageHandler abstract interface
**Reason**: The `MessageHandler` base class conflates two unrelated handler types (HTTP byte-level vs TLV frame-level). TLV handlers bypass it entirely with no-op `OnMessage()` stubs. Handler ownership is now managed by the parser's callback lambda via `unique_ptr` capture, making the polymorphic interface unnecessary.
**Migration**: Handler classes become standalone concrete types. `GatewayTlvHandler::HandleFrame()`, `NodeagentTlvHandler::HandleFrame()`, and HTTP handlers' `HandleMessage()` are called directly by the parser's callback. No virtual dispatch through a base class.

## MODIFIED Requirements

### Requirement: HttpEchoHandler implementation
The system SHALL provide an `HttpEchoHandler` class (not inheriting from any base) with a `HandleMessage(std::span<const std::byte> msg, Connection& conn)` method that formats an HTTP 200 OK response with the received body as plain text.

#### Scenario: HttpEchoHandler echoes a request body
- **WHEN** `HttpEchoHandler::HandleMessage` is called with an HTTP request body
- **THEN** it SHALL format an HTTP/1.1 200 OK response with `Content-Type: text/plain`, `Connection: close`, and the body as the response payload
- **AND** it SHALL call `conn.Write(response_bytes)` to send the response

#### Scenario: HttpEchoHandler closes connection after response
- **WHEN** the HTTP response has been fully written
- **THEN** the connection SHALL be closed (via the existing end-of-stream lifecycle in Connection)
