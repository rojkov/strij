# Tasks: Capacity-released events

## 1. BoundedQueue primitive

- [x] 1.1 Add `BoundedQueue<T>` template (`Push()->bool` rejecting on full, `TryPop()->std::optional<T>`, `Peek()`, `Size()`, `Empty()`, `Full()`, `MaxSize()`, `Clear()`) over `std::deque` in `src/common/core/utils/bounded_queue.hh`, namespace `strij::utils`
- [x] 1.2 Add `bounded_queue_lib` target to `src/common/core/utils/BUILD.bazel`
- [x] 1.3 Add `bounded_queue_test.cc` covering: push-to-capacity, full-queue rejects Push, FIFO TryPop order, TryPop on empty is disengaged, Clear, MaxSize/Size/Full consistent

## 2. CAPACITY_RELEASED command

- [x] 2.1 Add `CAPACITY_RELEASED` to `event::Command::Type` in `include/strij/event/command.hh` (after `DEFERRED_DELETE`, preserving zero-value semantics of `ACTIVATE_READ`)
- [x] 2.2 Update `openspec/specs/command-handler/spec.md` main spec (sync `CAPACITY_RELEASED` enum value + pure-wakeup `args_ == nullptr` contract, per `openspec sync-specs`)

## 3. AdmissionController broadcast

- [x] 3.1 Add `void RegisterCapacityObserver(event::CommandHandler*)` to the public `AdmissionController` interface (`include/strij/nodeagent/admission_controller.hh`)
- [x] 3.2 Extend `AdmissionControllerImpl`: constructor gains `event::Dispatcher&`; add observer list member; in `Release()` (after successful counter decrement, non-underflow) submit `{type_=CAPACITY_RELEASED, destination_=observer, args_=nullptr}` to every registered observer via the dispatcher
- [x] 3.3 Update the `AdmissionControllerImpl` call site in `nodeagent_framework.cc:80` to pass `*dispatcher`
- [x] 3.4 Update existing `admission_controller_test.cc` constructor callsites to the new signature (keep all existing behavior assertions intact)
- [x] 3.5 Add broadcast test scenarios to `admission_controller_test.cc` against `MockDispatcher` + a test `CommandHandler` double: registered observer woken on release; empty observer list submits no commands; release-broadcasts-one-command-per-observer; underflow release does not broadcast

## 4. Push scheduler stay-inert proof

- [x] 4.1 Verify `PushLocalScheduler` does NOT call `RegisterCapacityObserver` and its `ProcessCommand` remains a no-op (no changes expected; confirm in passing tests)
- [x] 4.2 Confirm `push_local_scheduler_test` / `nodeagent_tlv_handler_test` continue to pass unchanged, demonstrating zero behavior change

## 5. Spec sync

- [x] 5.1 `openspec sync-specs` to fold the delta requirements into main specs (`node-local-scheduler`, `command-handler`, `bounded-queue`)
- [x] 5.2 Run `make test` and `make check` (include purity + namespace coherence) and fix any fallout