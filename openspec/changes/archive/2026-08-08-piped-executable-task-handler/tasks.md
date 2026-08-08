## 1. Task protocol schema

- [x] 1.1 Add `map<string,string> parameters = 4` to `Task` in `api/core/task/task.proto`
- [x] 1.2 Extend `test/core/task/task_protocol_test.cc` with a parameters round-trip case

## 2. Dispatcher poll operation

- [x] 2.1 Add `PreparePoll(Completable* io, uint8_t tag, int fd, uint32_t poll_mask)` to the `Dispatcher` interface (`include/strij/event/dispatcher.hh`)
- [x] 2.2 Implement `PreparePoll` in `DispatcherImpl` using `io_uring_prep_poll_add` (`src/core/event/dispatcher_impl.{hh,cc}`)
- [x] 2.3 Add `PreparePoll` mock to `MockDispatcher` (`test/mocks/event/mocks.hh`)

## 3. HTTP header capture

- [x] 3.1 Extend `HttpRequest` with a headers collection (`std::vector<std::pair<std::string, std::string>>`) in `src/core/io/llhttp_parser.hh`
- [x] 3.2 Wire llhttp `on_header_field` / `on_header_value` (and `*_complete`) callbacks in `LlhttpParser`, accumulating fragmented header fields/values
- [x] 3.3 Extend `test/core/io/llhttp_parser_test.cc` with header-capture and fragmented-value cases

## 4. Gateway parameter forwarding

- [x] 4.1 In `GatewayHttpHandler`, extract a pure free function `PopulateParametersFromHeaders(Task&, const std::vector<std::pair<std::string, std::string>>&)` that forwards `x-strij-*` headers into `task.parameters` (strip prefix, lowercase key); call it from `HandleMessage`; add `kStrijHeaderPrefix` and `kFunctionParameter` constants
- [x] 4.2 Extend `test/core/gateway/gateway_test.cc` testing `PopulateParametersFromHeaders` directly (mapping, case-insensitivity, non-prefixed headers ignored, `function` key) plus a light `HandleMessage` integration case

## 5. Streaming task results

- [x] 5.1 Change `ResultReceiver::Deliver` to `Deliver(std::span<const std::byte> value, bool is_final)` in `src/core/gateway/result_receiver_storage.hh`; update test receivers
- [x] 5.2 `GatewayTlvHandler`: compute finality with `!has_is_final() || is_final()`, pass it to `Deliver`, keep the receiver on non-final results and erase only on final
- [x] 5.3 Extract pure stateful `HttpResponseFramer` (header-declared) deciding framing from first call's `is_final` — first-final → `Content-Length` single response; first-non-final → `Transfer-Encoding: chunked` headers, per-body chunk frames, terminal `0\r\n\r\n` on final — returning `std::vector<std::vector<std::byte>>` frames; rewire `HttpResultReceiver::Deliver` to `for (frame : framer_.Next(body, is_final)) conn_.Write(frame)`
- [x] 5.4 Extend `test/core/gateway/gateway_test.cc`: `HttpResponseFramer` framing cases asserted directly on returned frames (single-shot vs chunked, hex chunk sizes, terminal chunk); `GatewayTlvHandler` retention case (intermediate result keeps receiver, final removes it)

## 6. Function resolver

- [x] 6.1 Add `FunctionResolver` interface with `Resolve(std::string_view reference)` and the `kFunctionParameter = "function"` constant (`src/core/extensions/function_resolver.hh`)
- [x] 6.2 Implement `LocalFunctionResolver` (returns reference as path; empty reference → error) in `src/core/extensions/function_resolver.cc`
- [x] 6.3 Expose `FunctionResolver()` on `FactoryContext` and hold it in `FactoryContextImpl` (`src/core/extensions/factory_context.{hh,cc}`)
- [x] 6.4 Build a `LocalFunctionResolver` in `src/exe/nodeagent/nodeagent.cc` and inject it into `FactoryContextImpl`
- [x] 6.5 Add `test/core/extensions/function_resolver_test.cc` for `LocalFunctionResolver`

## 7. piped_executable extension

- [x] 7.1 Add `api/extensions/task_handlers/piped_executable/piped_executable.proto` (`PipedExecutableTaskHandlerConfig`) with BUILD target
- [x] 7.2 Implement `ChildProcess` (per-task `event::Completable`): pipes, `posix_spawn` with stdio file actions, tags `kStdinWrite`/`kStdoutRead`/`kStderrRead`/`kExitPoll`, async stdin write with partial-write loop, stdout chunk reads, stderr reads logged locally, pidfd `PreparePoll` exit detection, drain-on-exit, cleanup and deferred teardown via a `DEFERRED_DELETE` command (the `Connection::onEndOfStream` pattern)
- [x] 7.3 Implement `PipedExecutableTaskHandler` (owns map of in-flight `ChildProcess`, implements `event::CommandHandler` handling `DEFERRED_DELETE`) and `PipedExecutableTaskHandlerFactory` using `context.FunctionResolver()`; add `DEFERRED_DELETE` to `Command::Type`
- [x] 7.4 Register the factory via `REGISTER_FACTORY_FULLY_QUALIFIED` and add the BUILD target (`piped_executable_task_handler_lib`)
- [x] 7.5 Add `test/extensions/task_handlers/piped_executable/piped_executable_task_handler_test.cc` using real pipes + a trivial child (e.g. `/bin/cat`) and `MockDispatcher`-driven completions
- [x] 7.6 Add `piped_executable` to the nodeagent config example and link the extension in `src/exe/nodeagent/BUILD.bazel`
- [x] 7.7 Extend `test/core/nodeagent/task_handler_manager_test.cc` with a `piped_executable` config entry

## 8. Documentation

- [x] 8.1 Update `AGENTS.md`: record `posix_spawn`, `pidfd_open`, and reaping `waitpid` as accepted non-io_uring synchronous calls
- [x] 8.2 Run `make build` and `make test`; fix any failures
