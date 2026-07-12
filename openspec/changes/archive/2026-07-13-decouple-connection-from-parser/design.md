## Context

The `io/` package has three classes with tangled responsibilities:

- **`LlhttpParser`** inherits `IOObject` but its real job is HTTP parsing. It serves as the IOObject for both reads *and* writes because `Connection` cannot.
- **`Connection`** manages the socket fd, dispatcher, and connection lifecycle — but is not an `IOObject`, so it routes I/O completions through the parser via callbacks.
- **`TcpListener`** creates concrete `Connection` objects and erases them on `CLOSE_CONNECTION` commands via `void*` casts.

The existing TODO at `llhttp_parser.hh:71-72` already identifies that `on_next_read_ready_` and `on_end_of_stream_` should be formalized into an interface.

## Goals / Non-Goals

**Goals:**
- `Connection` becomes an `IOObject`, directly handling read/write completions from the Dispatcher.
- `LlhttpParser` becomes a pure parser — no `IOObject` inheritance, no I/O awareness.
- Parsers own their read buffers and provide them to io_uring via `GetReadBuffer()`, achieving zero-copy reads (matching the existing `LlhttpParser` chunk behavior).
- `Connection` owns only the write buffer (`write_buf_`), since writes originate from `MessageHandler` data that must be stabilized before submission.
- New `ProtocolParser` abstract interface for all parsers (HTTP, TLV, etc.).
- New `MessageHandler` abstract interface for application-level message handling.
- TCP echo server continues to work identically after refactoring.

**Non-Goals:**
- Implementing a TLV parser (this change enables it but doesn't build it).
- Changing the Dispatcher or `io_uring` integration.
- Changing `Command` struct or the command dispatch mechanism.
- Adding new tests beyond ensuring existing behavior is preserved.

## Decisions

### Decision 1: Connection inherits IOObject

**Choice:** `Connection` inherits `event::IOObject` and registers itself with the Dispatcher for read/write completions.

**Why:** `Connection` already manages the fd and orchestrates I/O. Making it the IOObject eliminates the indirection where the parser handles I/O completions it doesn't own.

**Alternative considered:** Keep Connection as a non-IOObject and only inject the parser. Rejected — this leaves the architectural inversion in place and means every new parser must implement IOObject.

### Decision 2: Parser provides read buffers (zero-copy by default)

**Choice:** `LlhttpParser` drops `IOObject` inheritance. Parsers own their read buffers and expose them via `GetReadBuffer()`. Connection drives the read loop but does not own the read buffer. Interface:

```cpp
class ProtocolParser {
public:
  enum class Action { NeedMoreData, MessageComplete };

  virtual ~ProtocolParser() = default;

  // Provide a writable buffer for io_uring to read into.
  // Must remain valid until the next OnData() call.
  virtual std::span<std::byte> GetReadBuffer() = 0;

  // Process bytes_read bytes that were written into GetReadBuffer().
  // Data is in the parser's own buffer — process in-place, no copy needed.
  virtual Action OnData(size_t bytes_read) = 0;
};
```

Connection drives the loop using the parser's buffer:

```cpp
void Connection::HandleCompletion(uint8_t tag, int res, uint32_t) {
  if (tag == Read) {
    if (res > 0) {
      auto action = parser_->OnData(static_cast<size_t>(res));
      if (action == ProtocolParser::Action::NeedMoreData) {
        dispatcher_->PrepareRead(this, ReadTag, fd_,
                                 parser_->GetReadBuffer(), 0);
      }
    } else {
      onEndOfStream();
    }
  } else if (tag == Write) {
    onEndOfStream();
  }
}
```

**Why:** The io_uring contract requires the buffer to be valid until `HandleCompletion` returns. This is satisfied by the ownership chain: `Connection` → `parser_` (unique_ptr) → buffer. The parser owns its buffers and is alive throughout the connection, so the buffers are always valid when `HandleCompletion` fires. This achieves zero-copy reads — the kernel writes directly into the parser's accumulation buffers (e.g., `Chunk`), and `OnData()` processes data in-place. This matches the existing `LlhttpParser` chunk behavior and eliminates the extra memcpy that a Connection-owned buffer would introduce.

**Parser responsibility:** Each parser manages its own read buffer allocation. `LlhttpParser` uses its existing `Chunk` system. A TLV parser might use a fixed `std::array<std::byte, N>` member. The `GetReadBuffer()` contract is: return a writable span valid until the next `OnData()` call.

**Alternative considered:** Connection owns `read_buf_` and copies data to the parser. Rejected — introduces an unnecessary memcpy per read cycle. The ownership chain (Connection → parser → buffer) already guarantees validity, so Connection ownership provides no safety benefit while costing performance.

### Decision 3: Connection drives the read loop (no ReadFacilitator)

**Choice:** `Connection` controls the read loop: after each `OnData()`, it checks the `Action` return and re-arms via `PrepareRead(this, ..., parser_->GetReadBuffer(), 0)` if needed. No `ReadFacilitator` interface.

**Why:** The read loop is an I/O concern — which buffer to read into and when to issue the next read. The parser's only job is to say "I need more data" or "message complete." This keeps the parser free of I/O semantics while Connection handles all Dispatcher interaction. Fewer interfaces, fewer virtual calls, clear ownership.

**Note on writes:** Parsers don't write. The `MessageHandler` calls `conn.Write(data)`, which copies into `Connection::write_buf_`. The write buffer must be Connection-owned because the handler's data may be a temporary.

### Decision 4: MessageHandler abstracts response logic

**Choice:**

```cpp
class MessageHandler {
public:
  virtual ~MessageHandler() = default;
  virtual void OnMessage(std::span<const std::byte> msg, Connection& conn) = 0;
};
```

**Why:** The HTTP echo response is application logic, not connection logic. A `MessageHandler` implementation calls `conn.Write(response)` when it has data to send.

**Alternative considered:** Use a `std::function` callback instead of an interface. Rejected — an interface is more idiomatic for C++ class hierarchies, easier to test with mocks, and clearer in BUILD deps.

### Decision 5: Connection lifecycle stays in Connection

**Choice:** `Connection::OnEndOfStream()` continues to close the fd and submit `CLOSE_CONNECTION`. No change to the teardown path.

**Why:** Connection lifecycle is inherently a connection concern. The parser and handler don't need to know about it.

### Decision 6: TcpListener gains a parser/handler factory

**Choice:** `TcpListener` constructor takes a factory callable that produces `unique_ptr<ProtocolParser>` + `unique_ptr<MessageHandler>` pairs for each new connection.

**Why:** This lets different listeners serve different protocols without subclassing. A `make_http_factory()` convenience function provides the default behavior.

**Alternative considered:** Template `TcpListener` on parser/handler types. Rejected — templates in header-only code add compile-time coupling and make the `TcpListener` library harder to reuse.

## Risks / Trade-offs

- **[Risk] Read tag management** → `Connection` uses Dispatcher tags to distinguish read vs. write completions. Two tags needed: one for reads, one for writes. Simple enum, no complexity.

- **[Trade-off] Slightly more code in Connection** → Connection gains `HandleCompletion` dispatch logic and the read loop. But this is straightforward (~15 lines) vs. the current indirection through parser callbacks.

- **[Trade-off] Parser loses IOObject** → Any code that treats parsers as IOObjects (currently none outside Connection) would break. No such code exists.

- **[Risk] Buffer validity relies on ownership chain** → io_uring borrows the parser's buffer through `Connection` (the IOObject). Safety depends on `Connection` keeping `parser_` alive for its entire lifetime. This is guaranteed by `unique_ptr<ProtocolParser>` ownership. A `Connection` subclass that releases the parser early would violate this — but `Connection` is `final` and the parser is a private member, so this is not a practical concern.

- **[Risk] Parser must not reallocate during OnData()** → If `GetReadBuffer()` returns a pointer into a heap buffer, and `OnData()` triggers reallocation (e.g., vector resize), the buffer passed to io_uring could be freed before the next `HandleCompletion`. This is safe because `OnData()` is called synchronously within `HandleCompletion`, and `GetReadBuffer()` is only called after `OnData()` returns. The sequence is: `OnData()` completes → `GetReadBuffer()` returns new stable pointer → `PrepareRead()` submits to io_uring. No overlap.

## Migration Plan

This is an internal refactoring with no external API or deployment concerns. Steps:

1. Create new interfaces (`ProtocolParser`, `MessageHandler`)
2. Refactor `LlhttpParser` to implement `ProtocolParser` (drop IOObject, expose `GetReadBuffer()` returning existing chunk spans, `OnData(size_t)` processing in-place)
3. Create `HttpEchoHandler` implementing `MessageHandler`
4. Refactor `Connection` to implement `IOObject`, own `write_buf_`, drive read loop using parser's buffer
5. Update `TcpListener` to use factory pattern
6. Update BUILD files
7. Verify with `make build` and `make test`
