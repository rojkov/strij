## Context

The logging system uses a singleton `Logger` with a dedicated `io_uring` event loop thread. Each application thread registers a `LogFrontend` (lock-free SPSC queue) via `LOG_REGISTER_THREAD()`. LOG macros check `thread_local LogFrontend* local_context_` — if null, messages are silently dropped.

In both `gateway.cc` and `nodeagent.cc`, configuration loading and validation happen before `logger.Run()` and `LOG_REGISTER_THREAD()`, meaning early log messages (especially errors and warnings) are lost. The same gap exists after `Logger::Stop()` during shutdown.

The project convention is "no synchronous system calls, use `io_uring`", but this is a bootstrapping path where `io_uring` is not yet available.

## Goals / Non-Goals

**Goals:**
- Emit log messages to stderr when the logger thread is not running.
- Use a `[early]` marker to distinguish fallback output from normal log output.
- Cover both pre-registration and post-shutdown periods.
- Update `AGENTS.md` to document the synchronous-call exception for bootstrap/fallback paths.

**Non-Goals:**
- Buffering early messages for replay into the normal logger (Approach 2 from exploration — rejected as unnecessary complexity).
- Changing the normal logging path or its formatting.
- Adding any new dependencies.

## Decisions

### 1. Fallback mechanism: `write(2)` to `STDERR_FILENO`

**Choice:** When `local_context_ == nullptr`, format the log line into a stack buffer and call `write(STDERR_FILENO, buf, len)`.

**Alternatives considered:**
- **Ring buffer + drain:** Store early entries in a fixed ring buffer, replay when logger starts. Rejected — adds state management complexity, and the early period typically has few messages. Direct stderr output is simpler and gives immediate visibility.
- **`fprintf(stderr, ...)`:** Slightly simpler API, but `write(2)` is more minimal and avoids stdio buffering concerns.

**Rationale:** `write(2)` is a POSIX async-signal-safe function. It's technically synchronous but will never block for stderr (a kernel-side pipe/fd). This is acceptable in a bootstrap path before `io_uring` is available.

### 2. Formatting in `log_impl()` template

**Choice:** Since `log_impl()` is a template with full `Args...` type information, format directly using `std::format_to_n` into a stack buffer. No need for the type-erased `pack_arg`/`format_fn_` mechanism.

**Rationale:** The type-erased path exists solely for the SPSC queue boundary. The fallback path doesn't cross that boundary, so direct formatting is simpler and avoids the 512-byte `args_data_` packing overhead.

### 3. Output destination: stderr (not stdout)

**Choice:** Fallback logs go to `STDERR_FILENO`. Normal logs go to `std::cout` (stdout).

**Rationale:** stderr vs stdout already provides visual separation in a terminal. If both go to the same destination, the `[early]` marker alone distinguishes them. Using stderr also means fallback logs are less likely to be buffered or interleaved with application output.

### 4. Clear `local_context_` on `Logger::Stop()`

**Choice:** Set `local_context_ = nullptr` in `Stop()` after joining the logger thread. This ensures any late log calls after shutdown also hit the fallback path.

**Alternatives considered:**
- **`is_running` flag:** More explicit, but adds a second variable to check. The null check is sufficient since the two states (running/not-running) map directly to (non-null/null).
- **Don't worry about post-shutdown:** Risky — if anything logs during cleanup, it silently vanishes.

### 5. `AGENTS.md` update

**Choice:** Add a note that synchronous system calls (like `write(2)`) are acceptable when `io_uring` is not available, specifically for bootstrap and fallback paths.

**Rationale:** Documents the exception without weakening the general "no sync syscalls" rule.

## Risks / Trade-offs

- **Stack buffer overflow** → Truncation of long messages. Mitigated by using a 1024-byte stack buffer; truncation is acceptable for a fallback path.
- **`write()` atomicity** → POSIX doesn't guarantee atomicity for `write()` to file descriptors (only pipes/socket with `<= PIPE_BUF`). In practice, short writes to stderr don't interleave on Linux. Acceptable for a fallback.
- **`[early]` marker after shutdown** → The name "early" is slightly misleading for post-shutdown logs. Acceptable — the marker's purpose is identification, not temporal accuracy.
