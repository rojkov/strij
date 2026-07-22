## Context

The gateway sends TLV frames to nodeagents via `TlvSender`, which holds a raw fd and calls `::write()` synchronously. The nodeagent receives these frames and responds via `Connection::Write()` — the async io_uring path. The asymmetry means the gateway's event loop blocks on every task submission.

Additionally, `TcpConnector::Connect()` returns a raw `int fd` that is shared between the `Connection` object (reading via io_uring) and the `TlvSender` (writing via syscall). Two objects own the same fd with no lifecycle coordination.

Both `TlvSender::SendFrame()` and `NodeagentTlvHandler::HandleFrame()` manually serialize the identical TLV wire format `[type_id:1][length:4][value:N]` with hand-rolled `memcpy`/`htonl` code.

## Goals / Non-Goals

**Goals:**
- Eliminate all synchronous syscalls on the event loop (except the out-of-scope `connect()`)
- Eliminate the shared-fd ownership issue between TlvSender and Connection
- Handle partial writes from io_uring (short writes due to TCP send buffer pressure)
- Extract duplicated TLV serialization into a shared function
- Keep the single-threaded event loop model (no write queue needed)

**Non-Goals:**
- Making `connect()` async (out of scope)
- Adding a write queue (single-threaded event loop guarantees serialization)
- Changing the `ProtocolParser` interface
- Changing `TlvParser` or `LlhttpParser`
- Changing handler logic in `GatewayTlvHandler` or `GatewayHttpHandler` beyond the write path

## Decisions

### D1: Eliminate TlvSender, use Connection::Write() directly

**Choice:** Remove `TlvSender` entirely. `GatewayHttpHandler` serializes the TLV frame and calls `Connection::Write()` on the nodeagent connection.

**Rationale:** `Connection::Write()` already implements the async io_uring write path with proper buffer lifetime management. Reusing it avoids duplicating io_uring integration in a new class. It also eliminates the shared-fd problem — there's only one `Connection` per socket, owning the fd exclusively.

**Alternatives considered:**
- Making TlvSender an IOObject with its own PrepareWrite: duplicates Connection's write logic and buffer management. Still shares the fd.
- TlvSender with its own TCP connection (separate fd): wastes a file descriptor per nodeagent. Two TCP connections where one suffices.

### D2: TcpConnector::Connect() returns `Connection*`

**Choice:** Change the return type from `int` to `Connection*`. The `Connection` is owned by `TcpConnector::connections_` and its lifetime is tied to the `TcpConnector`.

**Rationale:** Callers need the `Connection` object to call `Write()`. Returning the raw fd was the root cause of the shared-fd problem. The `Connection*` is valid for the lifetime of the `TcpConnector`, which lives in `main()` alongside all other long-lived objects.

**Safety:** `GatewayHttpHandler` stores `vector<Connection*>&` — a reference to a vector in `main()`. The `Connection` objects are owned by `TcpConnector` which lives in the same scope. If the nodeagent disconnects, `TcpConnector::ProcessCommand(CLOSE_CONNECTION)` removes the `Connection` from its vector, but the raw pointer in `GatewayHttpHandler`'s reference would dangle. This is acceptable for now because: (a) disconnection currently means the nodeagent is gone and the gateway is shutting down, and (b) the same lifetime risk exists today with the shared raw fd.

### D3: Partial write handling via cursor (not erase)

**Choice:** Add `size_t write_offset_` to `Connection`. On partial write completion, advance the offset and resubmit the remaining span. Clear `write_buf_` and reset offset on completion or error.

**Rationale:** `std::string::erase()` from the front is O(n) — shifts all remaining bytes. A cursor is O(1) and avoids reallocation. The buffer stays intact for io_uring's lifetime contract.

**Assertion:** `Connection::Write()` asserts `write_buf_.empty()` to catch accidental concurrent writes. This is safe because the single-threaded event loop processes completions sequentially, and `Write()` is only called from completion handlers. A read completion (which triggers `HandleMessage` → `Write()`) and a write completion for the same Connection cannot overlap in the same batch because the read is re-armed after each completion, and the new read SQE won't complete until the next `io_uring_submit_and_wait()`.

### D4: SerializeTlvFrame as a free function

**Choice:** Add `auto SerializeTlvFrame(uint8_t type_id, std::span<const std::byte> value) -> std::vector<std::byte>` to `tlv_frame.hh/.cc`.

**Rationale:** Both `TlvSender` (being removed) and `NodeagentTlvHandler` build the same wire format. Extracting it eliminates duplication and provides a single source of truth for the serialization. A free function is appropriate because `TlvFrame` is a data struct with no state.

**Caller responsibility:** The caller composes the value span. For task frames, this means prepending `[task_id:8]` to the payload before calling `SerializeTlvFrame`. This keeps the function low-level and composable.

### D5: Connection write buffer type changes to `std::vector<std::byte>`

**Choice:** Change `write_buf_` from `std::string` to `std::vector<std::byte>`.

**Rationale:** The buffer stores raw binary frames, not text. `std::vector<std::byte>` is the semantically correct type. It eliminates the `std::as_bytes()` wrapper currently needed to convert `char*` to `std::byte*`, and makes the `PrepareWrite` span construction a direct `std::span<const std::byte>(write_buf_.data(), write_buf_.size())`.

## Risks / Trade-offs

- **[Connection lifetime with GatewayHttpHandler]** → `GatewayHttpHandler` holds `vector<Connection*>&` pointing to connections owned by `TcpConnector`. If a nodeagent disconnects and the `Connection` is destroyed, the pointer dangles. Mitigated by the current architecture where disconnection triggers shutdown. Can be hardened later with shared ownership or a connection registry.
- **[Partial write resubmission on error]** → If `io_uring_prep_write` returns a negative error during partial write handling, the remaining data in `write_buf_` is lost. This is acceptable — a write error on a TCP socket typically means the connection is broken and the `Connection` should be cleaned up. Future work: detect write errors and trigger end-of-stream handling.
- **[Tests use socketpair + synchronous reads]** → Existing `TlvSenderTest` tests create socketpairs and use synchronous `::read()` to verify wire format. These tests will be restructured to test `SerializeTlvFrame()` as a pure function (no socket needed) and the async write path through `Connection`.
- **[No backpressure signaling]** → If the nodeagent is slow, tasks accumulate in io_uring's write queue. The gateway doesn't yet have flow control. This is a pre-existing limitation, not introduced by this change.
