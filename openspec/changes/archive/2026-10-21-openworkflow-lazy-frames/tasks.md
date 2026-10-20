# Tasks

## 1. Frame substrate

- [x] 1.1 Add `frame.hh`/`frame.cc` with `ChildOutcome`, `ExecutionFailure`, `FrameResult`, the `FrameContext` interface, and the `Frame` base (unified lifecycle plus pure `Enter`/`Advance`/`Abandon`); add both files to `openworkflow_task_handler_lib`. Verify: `bazel build //src/nodeagent/extensions/task_handlers/openworkflow:openworkflow_task_handler_lib` succeeds.
- [x] 1.2 Implement the shared lifecycle transitions (`Pending`→`Active`, `Active`→`Completed`/`Failed`/`Abandoned`, terminal no-op on abandon) and declare `CreateRootFrame(const Document&)`. Verify: a frame unit test asserts the allowed transitions and that abandoning a completed frame is a no-op.

## 2. Sequence frame

- [x] 2.1 Implement `SequenceFrame` storage over `std::span<const NamedTask>`, `Enter`, and `materializeChild` for `do`/`fork`, with no subtree prebuild. Verify: a frame test with a fake context shows only the current step's frame exists after entry and a sibling step is materialized only after the cursor advances.
- [x] 2.2 Implement `SequenceFrame::Advance` for call/do/fork, `Suspended` on an outstanding child, and `Completed(value)` when the cursor passes the last step. Verify: frame tests cover a two-step sequence completing one step at a time and a nested `do` returning its value to the parent.
- [x] 2.3 Implement `Suspend(child_id)` and `ApplyOutcome` (waiting cleared; data replaced and cursor advanced on success; stored `Failed` with origin/step on error). Verify: a frame test drives an async success and an async error and asserts the resumed/terminal state.
- [x] 2.4 Implement `SequenceFrame::Abandon` as mark-only plus recursive `ForgetChild` for the active child. Verify: a frame test with a waiting nested sequence asserts the fake context received the waiting child id and no frame was destroyed mid-abandon.

## 3. Fork frame

- [x] 3.1 Implement `ForkFrame::Enter` (branch creation and per-branch `data.Copy()` seeding) and `Advance` for join-all, compete, and the empty join-all fork. Verify: frame tests cover declaration-order results, first-terminal compete, and `[]` for an empty join-all fork.
- [x] 3.2 Implement branch-failure composition (`branch '<name>': ` plus `step '<origin>': ` only when the origin is not the branch, marked composed) and the non-JSON join failure. Verify: frame tests assert the exact composed strings for a direct branch failure, a nested-`do` branch failure, and a nested-fork join failure.
- [x] 3.3 Implement compete resolution abandoning remaining branches and returning the join value to the parent. Verify: an interpreter test shows a late loser outcome does not submit another child or change the result.

## 4. Interpreter as FrameContext

- [x] 4.1 Make `WorkflowInterpreter` privately implement `FrameContext`; move the existing call projection/child-body/submit/inline-drain code into `ExecuteCall` and the owner-map erase into `ForgetChild`. Verify: the handler library builds and all existing interpreter tests pass.
- [x] 4.2 Reduce `drive` to `root_->Advance(*this)`, the `pending_` drain loop, and terminalization of `Completed`/`Failed`; create the root in `Start` via `CreateRootFrame` and `Enter(seed)`. Verify: `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:interpreter_test` passes.
- [x] 4.3 Delete `FrameBuilder`, `BuildFrames`, `advanceSequence`, `advanceStep`, `advanceFork`, `executeCall`, `completeSequence`, `onBranchComplete`, `resolveFork`, `applyOutcome`, `abandonFrame`, `failSequence`, `children_`, `parent_sequence_`, `FrameKind`, and the production `dynamic_cast`s. Verify: the library builds and `grep -rn "dynamic_cast\|BuildFrames\|FrameBuilder" src/nodeagent/extensions/task_handlers/openworkflow` has no matches.
- [x] 4.4 Confirm validation still runs over the document before any submission and that construction assumes validated kinds. Verify: `UnknownKeyFailsBeforeAnySubmission`, `MalformedCallInsideForkBranchFailsBeforeAnySubmission`, and the other validation tests pass with zero submissions.

## 5. Tests and cleanup

- [x] 5.1 Remove `FrameTreeTest` from `interpreter_test.cc`; keep every behavior test unchanged. Verify: `interpreter_test` passes.
- [x] 5.2 Add `frame_test.cc` with a fake `FrameContext` and a BUILD target. Verify: `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:frame_test` passes.
- [x] 5.3 Add interpreter tests for failure composition inside a nested `do` in a fork branch and for a nested-fork join failure. Verify: both assert the expected `branch '...'`/`step '...'` message shapes.
- [x] 5.4 Run the full openworkflow test suite and the end-to-end test. Verify: `bazel test //test/nodeagent/extensions/task_handlers/openworkflow/...` passes.
- [x] 5.5 Run repository checks. Verify: `make check` and `make build` succeed.
