## 1. Queue-safe Connection write path

- [x] 1.1 Replace `Connection`'s `write_buf_`/`write_offset_` with `std::deque<std::vector<std::byte>> write_queue_` in `connection.hh`; update `Write(data)` to append a copy and submit the front buffer only when the queue was empty
- [x] 1.2 Update `Connection::HandleCompletion(kWrite)` to advance through the front buffer, pop it when complete, and submit the next buffer if the queue is non-empty; on `res <= 0` clear the whole queue and log
- [x] 1.3 Update `test/core/io/connection_test.cc`: FIFO drain of two queued writes (second submitted only after first completes), partial-write advancement, write-error clears queue, concurrent `Write` no longer asserts

## 2. OutboundMailbox

- [x] 2.1 Add `src/core/io/outbound_mailbox.hh` / `outbound_mailbox.cc`: `OutboundMailbox(Connection&)`, `Enqueue(vector<byte>)`, `RegisterOnClose(move_only_function<void()>) -> size_t`, `UnregisterOnClose(size_t)`, private `Close()` (friend `Connection`); `Enqueue` no-ops when inactive
- [x] 2.2 Build the mailbox inside `connection_lib` (added `outbound_mailbox.{hh,cc}` to `connection_lib` srcs/hdrs) instead of a separate `outbound_mailbox_lib` — a separate target would create a dependency cycle (`connection.hh` needs the mailbox header, `outbound_mailbox.cc` needs `connection.hh`)
- [x] 2.3 Wire `Connection`: construct `shared_ptr<OutboundMailbox>` in ctor, `Mailbox()` accessor, call `Close()` in `onEndOfStream()` (after `close(fd_)`) and in `~Connection()`
- [x] 2.4 Add `test/core/io/outbound_mailbox_test.cc`: Enqueue forwards to `conn.Write`; Enqueue after Close is a no-op; Close fires all callbacks exactly once; UnregisterOnClose prevents firing; RegisterOnClose on closed mailbox fires immediately; multiple callbacks all fire (register target in `test/core/io/BUILD.bazel`)

## 3. ResultSender interface and handle

- [x] 3.1 Add `RegisterOnClose`/`UnregisterOnClose` pure virtuals to `ResultSender` in `src/extensions/task_handlers/task_handlers.hh`
- [x] 3.2 Rewrite `ConnectionResultSender` (`result_sender.{hh,cc}`) to hold `shared_ptr<OutboundMailbox>` (copyable handle); `Send` serializes `TaskResult` → TLV `kResult` frame → `mailbox_->Enqueue`; forward lifecycle hooks; update `result_sender_lib` deps to include the new mailbox lib
- [x] 3.3 Implement the two new virtuals in `MockResultSender` in `test/mocks/extensions/extensions_mocks.hh` (mock them); `EchoTaskHandler` implements `TaskHandler`, not `ResultSender`, so no change is needed there

## 4. Nodeagent wiring

- [x] 4.1 Update `NodeagentTlvHandler::HandleFrame` to obtain `conn.Mailbox()` and pass a retainable `ConnectionResultSender` to `HandleTask`
- [x] 4.2 Extend `test/core/nodeagent/nodeagent_tlv_handler_test.cc` with an async mock handler that retains the sender and sends twice; assert both frames arrive in order

## 5. Contract and verification

- [x] 5.1 Rewrite the synchronous-only contract docstrings in `task_handlers.hh` (ResultSender, TaskHandler) and `result_sender.hh` to describe the async-capable contract (retain sender, zero-or-more `Send()`, last `is_final`, valid until teardown, sends after teardown dropped)
- [x] 5.2 Run `make test` and `make build` — all 13 test suites pass, full build succeeds. `make test_asan` is blocked by a pre-existing environment issue unrelated to this change: the `rules_foreign_cc` GNU Make bootstrap cannot run binaries under the clang toolchain (libc++ runtime not on the sandbox loader path, `configure: cannot run C compiled programs`); confirmed on the untouched `//src/core/utils:task_id_lib`. Also regenerated stale `toolchain/abs_path.bzl` (`carrot` → `strij`) left by the repo rename.
