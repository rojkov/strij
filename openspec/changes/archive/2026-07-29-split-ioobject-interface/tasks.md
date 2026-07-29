## 1. Core interface replacements

- [x] 1.1 Create `include/carrot/event/completable.hh` with `Completable` interface
- [x] 1.2 Create `include/carrot/event/command_handler.hh` with `CommandHandler` interface
- [x] 1.3 Remove `include/carrot/event/io_object.hh` — interfaces absorbed by the two new headers
- [x] 1.4 Update `include/carrot/event/command.hh`: `destination_` type changes from `IOObject*` to `CommandHandler*`
- [x] 1.5 Update `include/carrot/event/dispatcher.hh`: `Prepare*` methods take `Completable*`, `SubmitCommand` takes `CommandHandler*` (via Command struct)

## 2. Update core Dispatcher implementation

- [x] 2.1 Update `src/core/event/dispatcher_impl.hh`: implement `Completable` and `CommandHandler` instead of `IOObject`
- [x] 2.2 Update `src/core/event/dispatcher_impl.cc`: change `merge_with_tag` and CQE dispatch to cast to `Completable*`
- [x] 2.3 Update `DispatcherImpl::Run()` command delivery: `cmd.destination_->ProcessCommand(cmd)` already works unchanged (destination is now `CommandHandler*`)

## 3. Update Connection

- [x] 3.1 Update `src/core/io/connection.hh`: inherit only `Completable`, remove empty `ProcessCommand` override, change `owner_` type to `CommandHandler*`
- [x] 3.2 Update `src/core/io/connection.cc`: change `SubmitCommand` destination cast (already `owner_` as `CommandHandler*`)
- [x] 3.3 Update `src/core/io/tcp_listener.hh`: inherit `Completable` and `CommandHandler`
- [x] 3.4 Update `src/core/io/tcp_listener.cc`: no behavioural change (already implements both)

## 4. Update Node

- [x] 4.1 Update `src/core/io/node.hh`: inherit `Completable` and `CommandHandler`
- [x] 4.2 Update `src/core/io/node.cc`: no behavioural change

## 5. Update remaining IOObject implementations

- [x] 5.1 Update `src/core/logging/log_frontend.hh`: inherit `Completable` only, remove `ProcessCommand` override
- [x] 5.2 Update `src/core/logging/log_frontend.cc`: remove `ProcessCommand` method
- [x] 5.3 Update `src/core/common/signal_monitor.hh`: inherit `Completable` only
- [x] 5.4 Update `src/core/common/signal_monitor.cc`: no behavioural change

## 6. Update tests and mocks

- [x] 6.1 Update `test/mocks/event/mocks.hh`: replace `IOObject` with `Completable` and/or `CommandHandler` as appropriate
- [x] 6.2 Update `test/core/io/gateway_test.cc`: replace `IOObject` reference in `ProcessCommand` override
- [x] 6.3 No change needed — `log_frontend_test.cc` doesn't reference `IOObject`

## 7. Build and verify

- [x] 7.1 Run `make build` — fix any compilation errors
- [x] 7.2 Run `make test` — fix any test failures
- [x] 7.3 Run `make test_asan` — pre-existing toolchain issue (BootstrapGNUMake with clang), not related to change
- [x] 7.4 Verify no remaining references to `IOObject` in `src/` and `include/` (outside archived changes)
