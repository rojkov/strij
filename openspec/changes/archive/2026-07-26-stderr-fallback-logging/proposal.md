## Why

Log messages emitted before `Logger::Run()` and `LOG_REGISTER_THREAD()` are silently dropped because `local_context_` is `nullptr`. This means configuration errors, validation warnings, and startup diagnostics in `gateway.cc` and `nodeagent.cc` vanish without a trace. The same gap exists after `Logger::Stop()` during shutdown. There is already a TODO at `log.hh:27-28` acknowledging this.

## What Changes

- Add a `write(2)` to `STDERR_FILENO` fallback path in `log_impl()` when `local_context_ == nullptr`, with a `[early]` visual marker distinguishing these from normal logs.
- Clear `local_context_` in `Logger::Stop()` so the fallback also covers post-shutdown log calls.
- Update `AGENTS.md` to document that synchronous system calls (like `write(2)`) are acceptable when `io_uring` is not available (e.g., bootstrap/fallback paths).

## Capabilities

### New Capabilities

- `stderr-fallback-logging`: Fallback logging to stderr via `write(2)` when the logger thread is not running, covering pre-registration and post-shutdown periods.

### Modified Capabilities

(none)

## Impact

- **Code**: `src/core/logging/log.hh` (fallback path in `log_impl()`), `src/core/logging/logger.cc` (clear `local_context_` in `Stop()`).
- **Docs**: `AGENTS.md` (synchronous call exception).
- **No API changes** — this is purely additive behavior for an existing code path.
- **No new dependencies** — uses POSIX `write(2)` and existing `std::format_to_n`.
