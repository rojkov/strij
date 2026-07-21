# Carrot

C++23 gateway server and distributed node agent servers `io_uring`-based (Linux) event loop. Bazel 9.0.1 build.
No synchronous system calls. Use `io_uring` as much as possible.

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
- Custom macros in `bazel/build_system.bzl`: `carrot_cc_library`, `carrot_cc_test`, `carrot_cc_binary`, `carrot_cc_test_library`.
- **Include prefix** is auto-derived: package `include/carrot/event` → prefix `carrot/event`, package `src/core/logging` → prefix `core/logging`. Includes use the prefix path, not the filesystem path.
- Test visibility is auto-added for `src/` packages → `test/` counterparts.
- `carrot_cc_test` adds `@googletest//:gtest` automatically; **still need explicit `@googletest//:gtest_main`** for the test main.
- External C libs built via `rules_foreign_cc`: `liburing` (configure/make), `llhttp` (cmake).

## Code conventions

- **Headers:** `.hh`, **sources:** `.cc`, **header guards:** `#pragma once`
- **Method names** are capitalized when public to distinguish them visually from private ones.
- **Namespaces:** `carrot::common`, `carrot::event`, `carrot::io`, `carrot::logging`
- **Format:** `.clang-format` — column 100, left-aligned pointers, grouped includes.
- **Lint:** `.clang-tidy` with cppcoreguidelines/modernize/readability checks.
- **Default toolchain:** host compiler (GCC). **Clang toolchain** at `//toolchain:cc_toolchain_for_linux_x86_64` enables ASan/TSan and libc++.

## Architecture

- **Entrypoints:** `//src/exe/gateway:gateway` (`gateway.cc`) and `//src/exe/nodeagent:nodeagent` (`nodeagent.cc`)
- **Event loop:** `Dispatcher` (abstract) / `DispatcherImpl` (io_uring, 4096 entries). `IOObject` handles completions; `Command` struct carries type/target/args.
- **Logging:** Singleton `Logger` runs its own `DispatcherImpl` in a dedicated thread. Each thread registers a `LogFrontend` (lock-free SPSC queue). Macros: `LOG_DEBUG()`, `LOG_INFO()`, `LOG_WARNING()`, `LOG_ERROR()`, `LOG_REGISTER_THREAD()`.

## Testing

- Google Test 1.17.0 with GMock. Only one test: `//test/core/logging:log_frontend_test`.
- Mock: `//test/mocks/event:event_mocks_lib` provides `MockDispatcher`.
- Generated artifacts to ignore: `compile_commands.json`, `toolchain/abs_path.bzl`, `llvm-project-build/`, `.cache/clangd/`.
