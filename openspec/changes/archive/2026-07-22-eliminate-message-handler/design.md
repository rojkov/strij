## Context

The current architecture uses a `MessageHandler` abstract interface to decouple `Connection` from application-level message handling. Every handler inherits from `MessageHandler` and implements `OnMessage(span<byte>, Connection&)`. The `ConnectionFactory` returns a `pair<ProtocolParser, MessageHandler>`, and `Connection` stores both as `unique_ptr` members.

This worked for the initial HTTP echo case but breaks down for TLV handlers:

- `GatewayTlvHandler` and `NodeagentTlvHandler` override `OnMessage()` as no-ops
- Their real entry point is `HandleFrame(TlvFrame, Connection&)`, called directly by the `TlvParser` callback
- `Connection::handler_` is dead weight for TLV connections — stored but never called through the vtable
- Two unrelated handler interfaces (byte-level HTTP vs frame-level TLV) are forced through one base class

The parser already acts as the dispatch mechanism: its callback captures the handler and calls the right method. The only reason `MessageHandler` exists is to satisfy the `ConnectionFactory` return type and provide lifetime management for `Connection::handler_`.

## Goals / Non-Goals

**Goals:**
- Eliminate the `MessageHandler` base class and its dead vtable indirection
- Make handler ownership explicit and clean (parser's lambda owns the handler via `unique_ptr`)
- Simplify `Connection` to own only the parser
- Keep handler classes as standalone concrete types (no inheritance needed)
- Zero behavioral change — same dispatch, same lifetime, just cleaner ownership

**Non-Goals:**
- Changing the `ProtocolParser` interface
- Changing `TlvParser` or `LlhttpParser` implementations (they stay generic callbacks)
- Changing handler logic (all `HandleFrame`/`OnMessage` method bodies stay the same)
- Changing `ResultReceiverStorage`, `TlvSender`, `TlvFrame`, or any other supporting types

## Decisions

### D1: Parser callback owns the handler via `unique_ptr` capture

**Choice:** The `ConnectionFactory` returns `unique_ptr<ProtocolParser>`. The handler is captured as `unique_ptr` in the parser's callback lambda. `Connection` stores only `parser_`.

**Rationale:** The parser callback already captures the handler by raw pointer and calls the appropriate method. Moving ownership into the lambda is a minimal, natural change. The ownership chain becomes explicit:

```
Connection → parser_ → parser → callback lambda → handler (unique_ptr)
```

The parsers use `std::move_only_function` (C++23) instead of `std::function` for their callbacks, which allows move-only captures like `unique_ptr`. This avoids the `shared_ptr` overhead that `std::function` would require.

**Alternatives considered:**
- Type-erased keep-alive (`unique_ptr<void>`): works but loses clarity. The lambda capture approach is more idiomatic C++ and keeps the handler type visible.
- Parser takes ownership as a constructor parameter: couples the parser to the handler type, breaking the generic `ProtocolParser` abstraction.

### D2: `ConnectionFactory` returns `unique_ptr<ProtocolParser>` (not a pair)

**Choice:** Simplify the factory signature to return only the parser. Handler ownership is internal to the parser's callback.

**Rationale:** The pair was a workaround for two separate ownership roots. With the parser owning the handler, only one object needs to be returned and stored.

### D3: Handler classes become standalone (no base class)

**Choice:** Remove `MessageHandler` inheritance from all handler classes. They become concrete types with their own entry-point methods (`OnMessage` for HTTP, `HandleFrame` for TLV).

**Rationale:** These classes were never polymorphic in practice — the parser callback calls a specific method, not `OnMessage()` through a vtable. Removing inheritance eliminates the fake interface.

### D4: Rename `OnMessage` to `HandleMessage` on HTTP handlers

**Choice:** Rename `GatewayHttpHandler::OnMessage()` to `HandleMessage()` (and similarly for `TrivialEchoHandler`, `HttpEchoHandler`).

**Rationale:** After removing `MessageHandler`, `OnMessage` is no longer a virtual override. Renaming to `HandleMessage` makes it consistent with `HandleFrame` on the TLV side and signals it's a protocol-specific entry point, not an interface implementation.

## Risks / Trade-offs

- **[Lambda captures `unique_ptr` + `Connection&` by reference]** → The `Connection&` reference in the lambda must remain valid for the lambda's lifetime. This is safe because `Connection` owns the parser, which owns the lambda. The connection outlives everything it owns. This is the same lifetime relationship as before, just without the `handler_` member.
- **[No more polymorphic dispatch]** → If a future use case needs runtime handler substitution, we'd need to reintroduce an interface. This is unlikely given the current architecture where each connection has a fixed handler type determined at factory time.
- **[Tests call handler methods directly]** → Tests for `GatewayTlvHandler` and `NodeagentTlvHandler` call `HandleFrame` directly. Since the classes still exist (just without inheritance), tests are unaffected. No test refactoring needed.
- **[Parsers use `move_only_function`]** → The parsers switch from `std::function` to `std::move_only_function` (C++23). This is a minor interface change that enables `unique_ptr` capture in lambdas. The parsers are not used polymorphically, so this doesn't break any external contracts.
