## ADDED Requirements

### Requirement: Connection owns an OutboundMailbox
The system SHALL provide a `strij::io::OutboundMailbox` class owned by `Connection`. `Connection` SHALL construct the mailbox in its constructor, hold it as a `std::shared_ptr<OutboundMailbox>`, expose it via a `Mailbox()` accessor, and invoke `Close()` on it during teardown (in end-of-stream handling and in the destructor) before the connection is destroyed.

#### Scenario: Connection exposes its mailbox
- **WHEN** a `Connection` is constructed
- **THEN** `Connection::Mailbox()` SHALL return a valid `shared_ptr<OutboundMailbox>`

#### Scenario: Connection closes the mailbox on teardown
- **WHEN** the connection reaches end-of-stream
- **THEN** `OutboundMailbox::Close()` SHALL be invoked before the connection is destroyed

### Requirement: OutboundMailbox queues frames to the connection
`OutboundMailbox::Enqueue(frame)` SHALL forward the frame to `Connection::Write`, which SHALL append it to the connection's outbound queue. While the mailbox is inactive (after `Close()`), `Enqueue` SHALL be a no-op and SHALL NOT touch the connection.

#### Scenario: Enqueue writes through the connection
- **WHEN** `Enqueue(frame)` is called on an active mailbox
- **THEN** the frame SHALL be handed to `Connection::Write`

#### Scenario: Enqueue after close is a no-op
- **WHEN** `Enqueue(frame)` is called on a mailbox whose `Close()` has been called
- **THEN** the frame SHALL be dropped and the connection SHALL NOT be touched

### Requirement: OutboundMailbox lifecycle callbacks
`OutboundMailbox` SHALL support registering zero or more close callbacks via `RegisterOnClose(std::move_only_function<void()>) -> std::size_t` and `UnregisterOnClose(std::size_t)`. `Close()` SHALL set the mailbox inactive and fire every registered callback exactly once on the event-loop thread, then clear the callback list. Registering on an already-closed mailbox SHALL fire the callback immediately rather than storing it.

#### Scenario: Close fires registered callbacks
- **WHEN** `Close()` is invoked on a mailbox with two registered callbacks
- **THEN** both callbacks SHALL be invoked exactly once

#### Scenario: Unregister prevents firing
- **WHEN** a callback is unregistered via its token and then `Close()` is invoked
- **THEN** the unregistered callback SHALL NOT be invoked

#### Scenario: Register after close fires immediately
- **WHEN** `RegisterOnClose(cb)` is called on a mailbox that is already closed
- **THEN** `cb` SHALL be invoked immediately and SHALL NOT be stored
