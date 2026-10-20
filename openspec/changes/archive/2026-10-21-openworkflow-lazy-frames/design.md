# Design

## Context

See `proposal.md` — Why. Constraints that shape the approach, all inherited from the landed interpreter (`openspec/specs/openworkflow-interpreter`) and the completed fork change:

- **`Submit` is not a barrier.** A local child can complete inside `Submit`; the `kAdvancing`/`pending_` reentrancy gate and the per-child inline drain must survive unchanged.
- **Abandonment is free.** `ChildTaskSubmitter` has no cancel. A late outcome is harmless once its child id is no longer in the owner map, so abandonment means "forget the waiting ids and mark the frames".
- **Validation stays a separate document walk.** Every data-independent failure must occur before the first submission. Lazy construction may therefore assume a body is one of the validated kinds (`call`, `do`, `fork`).
- **The document outlives the run.** `WorkflowInterpreter` holds `document_` for the whole run; frames currently store `const NamedTask*` into it, so span/string-view storage adds no new lifetime requirement.
- **No public API is involved.** Frames, `BuildFrames`, and `WorkflowInterpreter` are internal to the handler package; only `test/` peers include them.
- **Existing behavior suite is the acceptance contract**, including the tested failure strings (`branch 'a': ...`), submit ordering, and late-outcome dropping.

## Goals / Non-Goals

**Goals:**

- Make frame progression a single recursive relation (`Advance` calls `Advance`) instead of the builder triangle plus the execution triangle.
- Give each frame kind ownership of its state, transitions, completion, and abandonment.
- Materialize one frame when its step is entered; hold only the active path plus entered fork branches.
- Keep the interpreter's effects (parse, validate, jq, submit, routing, terminal result) intact and make the frame/effect boundary explicit.
- Preserve every observable behavior and failure message.

**Non-Goals:**

- New task kinds (`try`, `for`, `switch`, `set`, `wait`, `listen`, `run`, `emit`, `raise`) — they become frame kinds in later changes.
- Per-branch lazy entry inside a fork: branches are created on fork `Enter`, which is lazy relative to the document but not relative to the branch loop.
- Releasing a frame before its owning parent drops it.
- Cancelling outstanding children.
- Any change to configs, protos, wire messages, `ChildTaskSubmitter`, or `ResultReceiver`.
- Keeping `BuildFrames` or a tree-shape inspection API for tests.

## Decisions

### D1 — Frames own lifecycle and advancement; the interpreter supplies effects

`Frame` becomes an abstract base with `Enter(StepDataValue)`, `Advance(FrameContext&)`, and `Abandon(FrameContext&)`. `SequenceFrame` and `ForkFrame` own their state and implement those operations. `WorkflowInterpreter` privately implements a narrow context:

```
class FrameContext {
public:
  virtual ~FrameContext() = default;
  virtual auto ExecuteCall(SequenceFrame& owner, const NamedTask& step) -> absl::Status = 0;
  virtual void ForgetChild(std::string_view child_id) = 0;
};
```

`ExecuteCall` is the only place that touches the expression engine, child-id generation, the submitter, owner registration, and the inline-outcome drain. `ForgetChild` is the only mutation of the routing map requested by a frame. The context has **no** terminal callbacks; `drive` switches on the root's `FrameResult` and calls `succeed`/`fail` itself.

**Alternatives rejected:** (a) pass `WorkflowInterpreter&` to frames directly — smaller header dance, but frames would see parsing, validation, `on_complete_`, and lifecycle state they have no business seeing, and frame tests would need a full interpreter. (b) keep the methods on the interpreter and only introduce a result enum — leaves the upward completion mutation and the `dynamic_cast` dispatch in place.

### D2 — Lazy materialization, document-backed spans, and one active child

A sequence stores a `std::span<const NamedTask>` into the document rather than a copied pointer vector. On entering a `do`/`fork` step it materializes exactly one child frame and keeps it in `std::unique_ptr<Frame> child_`; there is no `children_` vector and no `parent_sequence_` back-pointer. A fork creates its branch sequences in `Enter`, so only forks on the executed path allocate branches.

```
SequenceFrame
  std::span<const NamedTask> steps_
  size_t cursor_
  StepDataValue data_
  std::unique_ptr<Frame> child_
  optional<string> waiting_child_id_
  optional<Branch> branch_         // {index, string_view name} for a fork branch
  optional<ExecutionFailure> failure_

ForkFrame
  bool compete_
  vector<unique_ptr<SequenceFrame>> branches_
  vector<optional<StepDataValue>> results_
  size_t completed_
```

`child_` matches the runtime truth: a sequence is suspended on at most one nested container. A completed child is destroyed by its parent after `child_->Advance` returns, so the live tree is the active path plus entered fork branches.

**Alternatives rejected:** keep the eager builder and only invert advancement — simpler test migration, but keeps a second traversal, `children_`, and back-pointers, and does not reduce the construction recursion.

### D3 — One `Advance` result; completion flows upward as a value

```
Advance(FrameContext&) -> Suspended | Completed(StepDataValue) | Failed(ExecutionFailure)
```

A sequence's step:
- `call`: `ctx.ExecuteCall`; a local error becomes a self-failure, an outstanding child yields `Suspended`, an inline outcome leaves the frame advanced and the loop continues.
- `do`/`fork`: materialize on first entry, `Enter(std::move(data_))`, then `Advance`. `Suspended` stops the parent; `Completed` moves the value back into `data_`, increments the cursor, drops the child; `Failed` propagates.

A fork collects branch `Completed` values, applies the compete/join-all policy, abandons the remaining branches, and returns the join value. `completeSequence`, `onBranchComplete`, and `resolveFork`'s parent write disappear; the parent applies the returned value.

```text
  SequenceFrame::Advance                 ForkFrame::Advance
    cursor -> call  --ExecuteCall-->       branch[i]->Advance
    cursor -> do    --child->Advance-->      Completed -> record
    cursor -> fork  --fork->Advance-->       Failed    -> compose, return
    cursor == end   -> Completed(data)       all/winner-> abandon rest,
                                              return Completed(join)
```

`Enter` on the base is what lets a parent treat a `do` and a fork uniformly. `StepDataValue` flows down by move; a fork takes ownership and copies into each branch.

### D4 — Return-propagated failure, composed at the nearest enclosing fork

`ExecutionFailure` carries `{status, origin, step, message, composed}`:

- A sequence's own step failure sets `origin = this`, `step = steps_[cursor_].name_`.
- A child failure propagates unchanged through non-branch sequences.
- The nearest enclosing `ForkFrame` receiving an uncomposed failure prefixes `branch '<name>': `, adds `step '<origin step>': ` only when the origin is not that branch, and marks it composed. A fork join failure (non-JSON branch result) is created already composed.
- `drive` turns a raw root failure into `fail(status, step, message)` and a composed one into `fail(status, "", message)`.

This reproduces today's rules exactly: a branch's direct call failure omits the step name, a nested `do` failure includes it, and an inner fork's join failure is named only by the inner branch. `origin` is a transient pointer to a live frame (or `this` when a failure is stored from an async outcome), so it does not reintroduce a persistent parent link.

**Alternatives rejected:** keep failure as a `FrameContext::Fail` side effect with loops checking a terminal flag. Smaller result type, but failure becomes an effect rather than a value, frames stay coupled to interpreter terminal state, and `try` interception would require re-plumbing failure sites.

### D5 — One lifecycle; waiting is data

```
Pending --Enter--> Active --> Completed
                     |  \
              error  |   \ Abandon
                     v    v
                  Failed  Abandoned
```

`Completed`, `Failed`, and `Abandoned` are terminal; `Abandon` from a terminal state is a no-op. `Waiting` is `waiting_child_id_` while `Active`, never a state. `try` later adds interception without changing this lifecycle. Both frame kinds share the enum and the terminal predicates; `Failed` exists because an asynchronous child error must survive from `ApplyOutcome` until the next `drive`.

### D6 — Reentrancy and outcome routing are unchanged

`OnChildOutcome` still looks up `owner_by_child_`; while `kAdvancing` it queues into `pending_`, otherwise it calls `owner->ApplyOutcome(...)` and then `drive()`. An inline outcome follows the same path: `ExecuteCall` sets the wait, registers the owner, submits, then drains **only its own** child's pending entry and applies it. `drive` drains the rest after the root advance returns. This keeps sibling outcomes from perturbing the advancing branch and preserves the "inline completion advances exactly once" behavior.

```text
  drive (kAdvancing)
    root_->Advance(ctx)
      SequenceFrame::Advance
        ctx.ExecuteCall(owner, step)
          Suspend(id) -> owner_by_child_[id] = &owner
          Submit(child) -> [receiver] OnChildOutcome -> pending_[id] (queued)
          pending_.extract(id) -> owner.ApplyOutcome  (data/cursor or failure)
        continue loop
    drain remaining pending_ -> ApplyOutcome -> Advance again
    Completed/Failed -> succeed/fail; otherwise kReady
```

### D7 — Lifetime: ownership, abandonment, and destruction are separated

- The parent sequence owns `child_`; a fork owns `branches_`. A frame is destroyed only by its owner, only **after** the owner's `Advance` call on it has returned.
- `Abandon` marks the frame `Abandoned` and recursively forgets waiting child ids; it never destroys or resets children. This makes it safe to abandon siblings inside `ForkFrame::Advance`'s branch loop.
- A completed branch or child has already had its outcome applied and its owner-map entry erased, so dropping it cannot strand a late outcome. `owner_by_child_` is erased before any frame that could own it is destroyed.
- `root_` lives until the interpreter is destroyed; completed/abandoned subtrees are reclaimed with their owning parent.

### D8 — File split and test strategy

`frame.{hh,cc}` holds `ChildOutcome`, `ExecutionFailure`, `FrameResult`, `FrameContext`, `Frame`, `SequenceFrame`, `ForkFrame`, and `CreateRootFrame`. `interpreter.hh` includes it; `frame.hh` never includes the interpreter. `interpreter.{hh,cc}` keeps parsing, validation, `Start`, `OnChildOutcome`, `drive`, `ExecuteCall`, rendering, and terminal result delivery. BUILD gains the two frame sources in `openworkflow_task_handler_lib`.

`FrameTreeTest` asserts a shape that no longer exists before execution; it is removed. A new `frame_test.cc` drives frames with a fake context (record `ExecuteCall`, control suspension with `Suspend`, deliver with `ApplyOutcome`) and covers one-step-at-a-time materialization, completion values, and recursive abandonment. The existing behavior suite remains unchanged and is the acceptance contract; two failure-composition cases are added where current coverage is thin.

## Risks / Trade-offs

- **[Failure-message drift]** → Composition is localized in `ForkFrame`/`drive` with explicit rules (D4); add tests for nested-`do`-in-branch and nested-fork join failures. Existing branch tests remain.
- **[Span/string-view lifetime]** → The document is immutable and owned by the interpreter for the whole run; `Start` runs once. Document this in `frame.hh`.
- **[Destruction while advancing]** → Only the owner destroys, only after `Advance` returns; `Abandon` is mark-only. No container is mutated during its own iteration.
- **[Losing shape-level test coverage]** → Replace with frame-level tests plus the unchanged behavior suite; the observable contract is submissions, values, order, and messages, not the prebuilt tree.
- **[Laziness changes branch entry timing]** → Branches are created and seeded on fork `Enter`, matching today's "all branches entered before advancing"; later branches are not submitted when an earlier compete branch wins inline, also matching today.
- **[Larger frame translation units]** → Keep per-kind `Advance`/`Abandon` in `frame.cc`; the interpreter class shrinks accordingly. No behavior is hidden in helpers.

## Migration Plan

One change, no feature flag, no data migration. Port in this order while keeping the behavior suite green after each step:

1. Add `frame.{hh,cc}` with the lifecycle, result/failure types, context interface, and frame declarations; add the BUILD sources.
2. Implement `SequenceFrame` (enter, lazy materialization, call/do/fork advance, apply-outcome, abandon).
3. Implement `ForkFrame` (enter/seed, join-all/compete, branch failure composition, abandonment).
4. Make `WorkflowInterpreter` the `FrameContext`: keep `ExecuteCall`/`ForgetChild`, move the loop into frames, reduce `drive` to advance + drain + terminalize.
5. Delete `FrameBuilder`, `BuildFrames`, the old advance/complete/resolve/fail-walk methods, and `FrameTreeTest`; add `frame_test.cc` and the missing failure-composition tests.

Rollback is reverting the change: the handler entrypoint, configs, and wire behavior are untouched.

## Open Questions

None blocking. Per-branch lazy entry and releasing completed frames earlier are deliberately deferred; both are internal optimizations that can be added without changing this design's contracts.
