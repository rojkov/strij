## Context

The nodeagent delivers task results to the gateway over a single shared `Connection`. Today the delivery path is synchronous-only:

- `ResultSender` is a raw `Connection&` wrapper; the interface docstring explicitly forbids retaining it past `HandleTask` (`src/extensions/task_handlers/task_handlers.hh:15-27`).
- `Connection::Write` owns exactly one buffer and asserts if a second `Write` happens while one is in flight (`src/core/io/connection.cc:48`).
- `NodeagentTlvHandler::HandleFrame` builds a stack-local `ConnectionResultSender` and calls `handler->HandleTask(task, sender)` synchronously.

Streaming handlers (next in line: `piped_executable`) need to retain their sender, emit multiple `TaskResult`s over time, and be told when the connection dies so they can tear down in-flight work (e.g. kill a child process). All code runs on a single event-loop thread, so no locking is required; the challenge is object lifetime, not concurrency.

## Goals / Non-Goals

**Goals:**
- `Connection::Write` accepts concurrent writes and drains them FIFO.
- A connection-owned `OutboundMailbox` provides a `shared_ptr`-shared outbound path that stays safe after the `Connection` is destroyed.
- `ResultSender` becomes a copyable handle (into the mailbox) so handlers can retain it; the interface gains `RegisterOnClose`/`UnregisterOnClose`.
- Teardown drops pending sends and fires registered callbacks on the event-loop thread.
- The `TaskHandler` contract flips to async-capable: `Send()` may be called zero or more times, last result marks `is_final`.

**Non-Goals:**
- No HTTP header capture, no `Task` proto params, no process supervision, no streaming HTTP responses. `piped_executable` and its end-to-end plumbing are subsequent changes that build on this substrate.
- No `io_uring_prep_writev`. Scatter-gather writes are a future throughput optimization; the deque-as-source-of-truth design keeps them a drop-in.
- No per-task mailboxes. One shared mailbox per connection; the gateway routes frames by task `id`.
- No change to `Connection::Write` return type or the read path.

## Decisions

### D1: `Connection::Write` becomes queue-safe
Replace `write_buf_`/`write_offset_` with `std::deque<std::vector<std::byte>> write_queue_`. `Write(data)` appends a copy; if the queue was empty, it immediately submits the front buffer. `HandleCompletion(kWrite)` advances through the front buffer (offset tracking), pops it when complete, and submits the next; on `res <= 0` it drops the whole queue and logs.

**Alternative considered:** keep single-buffer `Write` and serialize sends inside the mailbox (mailbox waits for each write completion before sending the next). Rejected: requires a completion hook threaded from `Connection` into the mailbox, more moving parts, and streaming bursts would stall on one in-flight write. Queue-safe `Write` is the simpler substrate.

**Why the deque matters for the future:** the deque is the single source of truth; a later `writev` path only needs to rebuild an iovec snapshot from it. The mailbox contract is unaffected.

### D2: `strij::io::OutboundMailbox`, owned by `Connection`
```cpp
class OutboundMailbox {
public:
  using CloseCallback = std::move_only_function<void()>;

  explicit OutboundMailbox(Connection& conn);   // conn_ only touched while active_

  void Enqueue(std::vector<std::byte> frame);   // no-op when !active_
  std::size_t RegisterOnClose(CloseCallback cb); // fires immediately if already closed
  void UnregisterOnClose(std::size_t token);

private:
  friend class Connection;
  void Close();  // active_=false, drop nothing (queue lives in Connection),
                 // fire all callbacks, clear list

  Connection& conn_;
  bool active_{true};
  std::vector<std::pair<std::size_t, CloseCallback>> close_callbacks_;
  std::size_t next_token_{0};
};
```
`Connection` constructs it (`std::make_shared<OutboundMailbox>(*this)`), exposes `Mailbox()`, and calls `mailbox_->Close()` in `onEndOfStream()` (immediately after `close(fd_)`) and defensively in `~Connection()`. `Enqueue` checks `active_` before dereferencing `conn_`, so a stale handle (held by a handler after teardown) is a safe no-op.

**Alternative considered:** the mailbox drives its own io_uring writes on the fd, independent of `Connection`. Rejected (D4): two writers on one fd risk mid-frame interleaving; keeping `Connection` the sole writer preserves framing.

**Alternative considered:** `Connection` stays generic and the nodeagent layer owns the mailbox. Rejected: spreads the teardown contract across two layers; the whole point is that `Close()` is colocated with teardown so nobody forgets it.

### D3: `ResultSender` interface gains lifecycle hooks; `ConnectionResultSender` becomes a handle
```cpp
class ResultSender {
public:
  virtual ~ResultSender() = default;
  virtual void Send(strij::task::TaskResult result) PURE;
  virtual std::size_t RegisterOnClose(std::move_only_function<void()> cb) PURE;
  virtual void UnregisterOnClose(std::size_t token) PURE;
};
```
`ConnectionResultSender` holds `std::shared_ptr<OutboundMailbox>` (copyable). `Send` serializes `TaskResult` → TLV `kResult` frame → `mailbox_->Enqueue(bytes)`. `RegisterOnClose`/`UnregisterOnClose` forward to the mailbox. `EchoTaskHandler` implements `TaskHandler` (not `ResultSender`), so it is unaffected; `MockResultSender` mocks the two new methods.

`NodeagentTlvHandler::HandleFrame` obtains `auto mailbox = conn.Mailbox();` and passes `ConnectionResultSender{std::move(mailbox)}` to `HandleTask`. The handler may retain it.

**Alternative considered:** keep the hooks off the interface and let handlers `dynamic_cast` to the concrete sender. Rejected: every async handler would need the cast; the interface is the contract.

### D4: Strict single-writer discipline
`Connection` remains the only object registering write ops on the fd. The mailbox queues *into* `Connection::Write`; it never touches the Dispatcher. This preserves frame ordering and prevents interleaving between result frames and any other writes (e.g. gateway error responses on the same path in future).

### D5: Teardown notification semantics
- Callbacks fire inside `OutboundMailbox::Close()`, which `Connection` calls before destroying itself — the fd is already closed (`close(fd_)` in `onEndOfStream`), so callbacks must be cleanup-only (kill child, erase state), never I/O back into the connection.
- `Close()` runs on the event-loop thread; no races with `HandleTask`/`Send`.
- `RegisterOnClose` on an inactive mailbox fires the callback synchronously (total contract, defensive).
- Handlers unregister on task completion; stale callbacks (handler forgot) no-op because the handler's per-task lookup by `task_id` finds nothing.

### D6: Contract docstring rewrites
`task_handlers.hh` and `result_sender.hh` docstrings that declare async delivery unsupported are rewritten to describe the async-capable contract (may retain sender, zero-or-more `Send()`, last marked `is_final`, valid until teardown, sends after teardown dropped).

### D7: Tests
- `connection_test`: queue FIFO drain (two `Write`s → two frames in order, second submitted only after first completes), write error drops queue, EOF closes mailbox.
- New `outbound_mailbox_test`: `Enqueue` → `conn.Write`; `Close` drops later sends; `RegisterOnClose` fires on `Close`; `UnregisterOnClose` prevents firing; `RegisterOnClose` on closed mailbox fires immediately; multiple callbacks all fire.
- `nodeagent_tlv_handler_test`: async mock handler retains sender, sends twice; both frames arrive in order. `MockResultSender` updated for new virtuals.
- `log_frontend_test` etc. unaffected.

## Risks / Trade-offs

- [Stale mailbox holds dangling `Connection&`] → `Close()` is invoked before `Connection` destruction and sets `active_=false`; `Enqueue` never dereferences `conn_` while inactive. Contract documented in the class.
- [Callback fires during teardown and touches the dead connection] → documented rule: callbacks are cleanup-only; the fd is already closed when they run.
- [Unbounded write queue under a slow consumer] → inherent to the design (memory bounded by peer drain rate); same exposure as today, only the single-buffer cap is removed. Frames are small; revisit backpressure when streaming lands.
- [Breaking the `Write`-assert surfaces latent double-write bugs elsewhere] → `assert` removed by design; the queue makes concurrent writes correct rather than erroring, which is the intended contract change.
- [Mock churn for the two new `ResultSender` virtuals] → mechanical; the mock mocks them, echo no-ops them.

## Migration Plan

Single-change landing. Order inside the change: queue-safe `Write` (+ tests) → `OutboundMailbox` (+ tests) → `ResultSender` interface + `ConnectionResultSender` handle + echo/mock updates → `NodeagentTlvHandler` wiring → docstring rewrites → full test run (`make test`). No deployment steps beyond a normal rebuild; the wire protocol (TLV frames, `Task`/`TaskResult` protos) is unchanged, so gateway and nodeagent stay compatible.

## Open Questions

None blocking. Follow-ups (not in this change): `piped_executable` handler consuming the async sender, process supervision, streaming HTTP response, optional writev.
