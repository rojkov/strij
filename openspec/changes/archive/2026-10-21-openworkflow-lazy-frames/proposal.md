# Proposal

## Why

The interpreter's execution state is currently split between `WorkflowInterpreter` and passive frame records. Construction, advancement, completion, abandonment, and failure each traverse the same tree differently: the builder is a mutual recursion, completion reaches upward into the parent, and the interpreter dispatches with `friend` access and `dynamic_cast`. That makes the control flow hard to reason about and forces every new task kind to edit several traversal sites. With `fork` landed and `try`/`for`/`switch` queued, this is the moment to consolidate the tree once.

## What Changes

- Frames own their lifecycle and advancement through virtual `Enter`/`Advance`/`Abandon`; `WorkflowInterpreter` supplies a narrow `FrameContext` (`ExecuteCall`, `ForgetChild`) instead of walking the tree with `friend` access.
- Construction becomes lazy: a frame is created when its step is first entered. Remove the eager `FrameBuilder`/`BuildFrames` pre-pass, the `children_` vector, and the `parent_sequence_` back-pointer. Frame spans and branch names stay views into the document, which outlives the run.
- Completion and failure travel upward as values. `Advance` returns `Suspended | Completed(value) | Failed(failure)`; as the failure unwinds, the nearest enclosing fork frame (the fork that owns the failing branch) prefixes the message with the branch's name, so a nested fork names only its own branch. `drive` turns a root failure into the terminal result. Remove `completeSequence`, `onBranchComplete`, `resolveFork`'s parent write, and `failSequence`'s parent walk.
- Failure is return-propagated, so the context has no terminal callbacks. The `kAdvancing`/`pending_` reentrancy gate, inline-outcome drain, and child-id ownership routing are unchanged.
- Unify the two frame lifecycles into one (`Pending`, `Active`, `Completed`, `Failed`, `Abandoned`); "waiting on a child" becomes a sequence field rather than a state.
- Split `frame.{hh,cc}` out of `interpreter.{hh,cc}`. The interpreter keeps parsing, validation, the drive loop, expression/child effects, and terminal result delivery.
- Replace the `FrameTreeTest` shape assertions with frame-level tests driven by a fake `FrameContext`, and keep the existing behavior suite as the acceptance contract.

No wire, config, protocol, or specification behavior changes; this is an internals-only refactor.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This is a pure refactor: every requirement in `openworkflow-interpreter` (step sequencing and suspension, the data chain, strict expression mode, pre-execution validation, and single final result delivery) is preserved exactly, including failure messages. `skip_specs: true` is set accordingly.

## Impact

- `src/nodeagent/extensions/task_handlers/openworkflow/interpreter.{hh,cc}` — frames removed; `WorkflowInterpreter` becomes a `FrameContext`.
- `src/nodeagent/extensions/task_handlers/openworkflow/frame.{hh,cc}` — new: frame lifecycle, lazy materialization, `Advance`/`Abandon`, failure composition.
- `src/nodeagent/extensions/task_handlers/openworkflow/BUILD.bazel` — add the frame sources to `openworkflow_task_handler_lib`.
- `test/nodeagent/extensions/task_handlers/openworkflow/interpreter_test.cc` — remove `FrameTreeTest`; all behavior tests remain.
- `test/nodeagent/extensions/task_handlers/openworkflow/frame_test.{cc}` and its BUILD target — new frame-level tests.
- No changes to `ChildTaskSubmitter`, `ResultReceiver`, artifact loading, configs, protos, or the handler entrypoint.
