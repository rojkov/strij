## ADDED Requirements

### Requirement: Fallback logging to stderr when logger thread is unavailable
The system SHALL emit log messages to `STDERR_FILENO` via `write(2)` when `local_context_` is `nullptr`, indicating the logger thread is not running. Fallback log lines SHALL include a `[early]` marker after the thread ID to distinguish them from normal logger output.

#### Scenario: Log before logger thread starts
- **GIVEN** the logger thread has not yet been started (`Logger::Run()` not called)
- **WHEN** `LOG_INFO("Loading config")` is invoked
- **THEN** the message is written to stderr in the format `I <timestamp> <tid> [early] <file>:<line> Loading config`

#### Scenario: Log after logger thread stops
- **GIVEN** `Logger::Stop()` has been called and `local_context_` is cleared
- **WHEN** `LOG_WARNING("Late message")` is invoked
- **THEN** the message is written to stderr with the `[early]` marker

#### Scenario: Normal logging is unaffected
- **GIVEN** the logger thread is running and `local_context_` is non-null
- **WHEN** any `LOG_*` macro is invoked
- **THEN** the message is routed through the existing SPSC queue path (no `[early]` marker, no stderr write)

### Requirement: Local context cleared on logger stop
The system SHALL set `local_context_` to `nullptr` during `Logger::Stop()`, after the logger thread has been joined.

#### Scenario: Stop clears local context
- **GIVEN** a thread has called `LOG_REGISTER_THREAD()` and `local_context_` is set
- **WHEN** `Logger::Stop()` completes
- **THEN** `local_context_` is `nullptr` for the calling thread

### Requirement: Fallback format matches normal format
Fallback log lines SHALL use the same field sequence as normal log output: severity character, timestamp (HH:MM:SS.ffffff), thread ID, source file, line number, and formatted message. The only difference is the `[early]` marker and output destination (stderr vs stdout).

#### Scenario: Fallback format consistency
- **GIVEN** the logger thread is not running
- **WHEN** `LOG_ERROR("port {} is invalid", 8080)` is invoked
- **THEN** the output on stderr matches `E HH:MM:SS.ffffff <tid> [early] <file>:<line> port 8080 is invalid`
