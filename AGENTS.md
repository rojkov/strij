# Strij

C++23 gateway server and distributed node agent servers `io_uring`-based (Linux) event loop. Bazel 9.0.1 build.
No synchronous system calls. Use `io_uring` as much as possible. Synchronous calls (e.g. `write(2)`) are acceptable when `io_uring` is not available (bootstrap and fallback paths).

## Commands (Makefile wrappers)

| Command | Action |
---------|--------|
| `make build` | `bazel build //...` |
| `make test` | `bazel test //...` |
| `make compiledb` | build + generate `compile_commands.json` (for clangd) |
| `make test_asan` | clang toolchain + AddressSanitizer on `//src/...` |
| `make test_tsan` | clang toolchain + ThreadSanitizer on `//src/...` |
| `make coverage` | `bazel coverage` + HTML report in `./coverage` |
| `make clang-tidy` | clang-tidy via `tools/run-clang-tidy.py` |

## Build system

- **Bazelisk** reads `.bazelversion` (9.0.1), Bzlmod with `MODULE.bazel`.
- Custom macros in `bazel/build_system.bzl`: `strij_cc_library`, `strij_cc_test`, `strij_cc_binary`, `strij_cc_test_library`.
- **Include prefix** is auto-derived: package `include/strij/event` → prefix `strij/event`, package `src/core/logging` → prefix `core/logging`. Includes use the prefix path, not the filesystem path.
- Test visibility is auto-added for `src/` packages → `test/` counterparts.
- `strij_cc_test` adds `@googletest//:gtest` automatically; **still need explicit `@googletest//:gtest_main`** for the test main.
- **Target naming convention:** libs → `_lib`, `_interface`, `_impl`, or descriptive suffix (e.g. `dispatcher_impl_lib`, `llhttp_parser_lib`, `command_interface`). Test targets: same as test file without `_test` suffix.
- **External deps:** `liburing` 2.14 via `configure_make`, `llhttp` 9.4.1 via `cmake`, `yaml_cpp` 0.7.0 via `http_archive` + inline BUILD. Bzlmod: `googletest` 1.17.0, `abseil-cpp` 20260107.1, `protobuf` 33.4, `rules_foreign_cc` 0.15.1.
- **Vendored third-party:** `rigtorp/SPSCQueue.h` (single-header, `cc_library` named `spsc_queue_lib`).
- **Root BUILD.bazel** builds `liburing` and `llhttp` via `rules_foreign_cc`.
- **Default toolchain:** host compiler (GCC). **Clang toolchain** at `//toolchain:cc_toolchain_for_linux_x86_64` enables ASan/TSan and libc++.

## Code conventions

- **Headers:** `.hh`, **sources:** `.cc`, **header guards:** `#pragma once`
- **Method names** are capitalized when public to distinguish them visually from private ones.
- **Private methods** are lowercase.
- **Constructors/destructors:** adhere to the C++ Rule of Five. Explicitly delete copy and move ctors if not needed to avoid unintended copies.
- **Struct/class members** use trailing underscore suffix (`success_`, `error_message_`) to distinguish them from scoped local variables.
- **Namespaces:** `strij::common`, `strij::event`, `strij::io`, `strij::logging`, `strij::gateway`, `strij::nodeagent`, `strij::config`, `strij::utils`, `strij::extensions`. Use relative namespace prefixes, eg. `event::Dispatcher` to reference `strij::event::Dispatcher` when inside `strij::io` namespace.
- **Format:** `.clang-format` — column 100, left-aligned pointers, grouped includes (std, system, "src", "exe", "test", rest).
- **Lint:** `.clang-tidy` with cppcoreguidelines/modernize/readability checks.
- **Tag dispatch:** classes that implement `Completable` use `private enum Tags : uint8_t { kX = 0, kY = 1 }` for tag constants.
- **`using` aliases:** `DispatcherSharedPtr = std::shared_ptr<Dispatcher>`, `ChunkPtr = std::unique_ptr<Chunk>`, `ProtocolParserPtr = std::unique_ptr<ProtocolParser>`, `ResultReceiverPtr = std::unique_ptr<ResultReceiver>`, `FactoryContextPtr = std::unique_ptr<FactoryContext>`, `ConnectionFactory = std::function<std::unique_ptr<ProtocolParser>(Connection&)>`.
- **Callbacks:** use `std::move_only_function<void(T)>` (C++23) for owning callbacks in parser/handler constructors.

## Architecture

- **Entrypoints:** `//src/exe/gateway:gateway` (`gateway.cc`) and `//src/exe/nodeagent:nodeagent` (`nodeagent.cc`)
- **Event loop:** `Dispatcher` (abstract) / `DispatcherImpl` (io_uring, 4096 entries). `Completable` handles completions via `HandleCompletion(tag, res, flags)`; `Command` struct carries `Type`/`destination_`/`args_`. `CommandHandler` receives commands via `ProcessCommand(Command)`.
- **Logging:** Singleton `Logger` runs its own `DispatcherImpl` in a dedicated thread. Each thread registers a `LogFrontend` (lock-free SPSC queue of 1024 `LogEntry`). `LogEntry` packs args into a 512-byte `args_data_` array with a `format_fn_` trampoline. Macros: `LOG_DEBUG()`, `LOG_INFO()`, `LOG_WARNING()`, `LOG_ERROR()`, `LOG_REGISTER_THREAD()`. Fallback: `write_stderr_fallback()` when no frontend registered. **String args** require custom `pack_arg` specializations (size-prefixed, in `log_frontend.hh`). Tests: `log_frontend_test.cc` validates pack/unpack round-trip.
- **I/O layer:** `Connection` (handles read/write via `Completable` tags `kRead`/`kWrite`), `TcpListener` (accepts multishot, owns `Connection` objects). `ProtocolParser` abstract interface with `GetReadBuffer()` → `OnData(size_t)` → `Action`. Implementations: `LlvhttpParser` (HTTP, wraps `llhttp`), `TlvParser` (TLV framing, 4096-byte buffer). `Chunk` (chunked buffer with body tracking).
- **Protocol:** `TlvFrame` struct (`type_id`, `value` span) with `SerializeTlvFrame()` helper. TLV types: `kTaskSubmission=0`, `kResult=1`, `kHeartbeat=2`.
- **Gateway:** `GatewayHttpHandler` routes HTTP `/tasks/{type}` requests to node connections. `GatewayTlvHandler` processes TLV result frames from node agents, delivers via `ResultReceiverStorage` (task_id → `ResultReceiver` storage). `HttpResultReceiver` writes TLV result back over HTTP connection. `Node` wraps a single node connection (states: `kInitial`→`kConnecting`→`kConnected`→`kDisconnected`). `NodeDirectory` manages node pool, provides round-robin via `GetNextNode()`. `ParseTaskType()` extracts task type from path.
- **Node agent:** `NodeagentTlvHandler` receives TLV frames (task submissions), parses via protobuf `Task`, and routes `task.type()` through a shared `TaskHandlerManager` (`GetHandler(type)`; null → log warning + drop; results delivered via `ConnectionResultSender` wrapping in `SerializeTlvFrame(kResult)`).
- **Extensions:** `Registry<FactoryInterface>` singleton template. `REGISTER_FACTORY(FactoryClass, FactoryInterface)` macro for simple names; `REGISTER_FACTORY_FULLY_QUALIFIED(FactoryClass, FactoryInterface, RegistrarName)` for namespace-qualified names. `FactoryContext` provides `Dispatcher()` and `Logger()` access. Extension categories: `NodeDiscovery` / `NodeDiscoveryFactory` (node discovery plugins; `StaticNodeDiscovery` uses a hardcoded address list) and task handlers (`ResultSender`, `TaskHandler` / `TaskHandlerFactory`, `EchoTaskHandler` in `extensions/task_handlers/echo`; loaded from `NodeAgentConfig.task_handlers` via `BuildTaskHandlerManager`). `FactoryContextImpl` is the concrete context implementation.
- **Config:** Template-based `LoadConfig<T>(path, cli_overrides)` → `absl::StatusOr<T>`. Uses protobuf config types (`GatewayConfig`, `NodeAgentConfig`) with YAML loading via `yaml_cpp`. Override precedence: defaults → YAML → env vars (`STRIJ_{APP}_{FIELD_PATH}`) → CLI overrides (`field.subfield=value`). Array env: `STRIJ_GATEWAY_NODE_CONNECTIONS__0__ADDRESS`. CLI flags via `absl::Flags`.
- **Signal handling:** `SignalMonitor` uses `signalfd` + `Completable` interface for graceful shutdown.
- **Utils:** `GenerateTaskId()` returns a random hex string.
- **Protobuf:** `api/core/task/task.proto` (`Task`/`TaskResult`, `TaskResult.is_final` optional; absence means final — consumers treat `!has_is_final() || is_final()` as final, since proto3 forbids explicit defaults), `api/core/config/*.proto` (config schemas + `ExtensionConfig` extension proto), `api/extensions/task_handlers/echo/echo_task_handler.proto`, `api/extensions/node_discovery/static/static_node_discovery.proto`.

## Testing

- **9 test suites:** `config_loader_test`, `gateway_test`, `connection_test`, `llhttp_parser_test`, `tlv_frame_test`, `tlv_parser_test`, `log_frontend_test`, `nodeagent_tlv_handler_test`, `task_id_test`.
- Google Test 1.17.0 with GMock. Tests use `//test:mocks/event:event_mocks_lib` (`MockDispatcher`).
- **Common mocks** in `test/mocks/common/common_mocks.hh`: `TrivialParser` (dummy ProtocolParser returning `NeedMoreData`), `DummyOwner` (no-op CommandHandler). Include via `"test/mocks/..."` path.
- **Test pattern:** use anonymous namespace inside test file, `namespace strij::X { namespace { ... } }`. Wrap `TEST_F`/`TEST` in `NOLINTBEGIN(modernize-use-trailing-return-type)` / `NOLINTEND`.
- Generated artifacts to ignore: `compile_commands.json`, `toolchain/abs_path.bzl`, `llvm-project-build/`, `.cache/clangd/`.
