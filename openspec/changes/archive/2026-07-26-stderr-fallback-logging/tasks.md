## 1. Logger shutdown cleanup

- [x] 1.1 In `src/core/logging/logger.cc`, clear `local_context_` to `nullptr` in `Logger::Stop()` after joining the logger thread

## 2. Fallback logging in log_impl

- [x] 2.1 Add a `write_stderr_fallback` helper function in `src/core/logging/log.hh` that formats severity, timestamp, tid, `[early]` marker, file:line, and message into a stack buffer and writes to `STDERR_FILENO`
- [x] 2.2 In `log_impl()`, add an `else` branch to the `local_context_` null check that calls `write_stderr_fallback` with the formatted message

## 3. Documentation

- [x] 3.1 Update `AGENTS.md` to note that synchronous system calls (e.g. `write(2)`) are acceptable when `io_uring` is not available (bootstrap/fallback paths)

## 4. Verification

- [x] 4.1 Run `make build` to confirm compilation
- [x] 4.2 Run `make test` to confirm no regressions
