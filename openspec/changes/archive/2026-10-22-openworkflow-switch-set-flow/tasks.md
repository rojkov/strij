# Tasks

## 1. Expression slot

- [x] 1.1 Add `TypedSlot::kSwitchWhen` with its `TypedSlotName` entry and `$context`-only variable mapping in `expression.{hh,cc}`, and verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:expression_test` passes
- [x] 1.2 Add expression tests covering a bare `when` (malformed), a wrapped valid `when`, a `when` that reads `$context`, and a `when` with an invalid program, and verify `expression_test` passes

## 2. Flow-directive substrate

- [x] 2.1 Add `FlowKind`/`FlowDirective` and `ResolveFlowDirective(scope, then)` to `frame.{hh,cc}`, covering `continue`, `exit`, `end`, forward jump, backward jump, and an unknown target, and verify new `frame_test` cases pass
- [x] 2.2 Add `Ended` to `FrameResult`, propagate it through `SequenceFrame::advanceContainer` and `ForkFrame` (abandoning siblings), and handle it in `terminalize`, and verify tests for `exit` in a nested sequence, `end` in a nested sequence skipping ancestor `output.as`/`export.as`, `end` in a fork branch abandoning siblings, and `end` at root still applying document `output.as`
- [x] 2.3 Apply a step's directive after `output.as`/`export.as` on both completion sinks (`ApplyOutcome` and `advanceContainer`) with a pending result consumed by `Advance`, and verify tests for a resumed call with a jump, an inline local completion with a jump, a container with `exit`, and no directive on a skipped or failed step
- [x] 2.4 Add the per-sequence synchronous-transition counter reset on each `Advance`, with a generous default and a test-injectable budget, failing `INTERNAL` and naming the step when exceeded, and verify tests for a self-jumping step, a two-task cycle, a wide synchronous fork that does not trip the guard, and a loop that suspends on a child and resets

## 3. Inline task kinds

- [x] 3.1 Add `FrameContext::EvaluateSet` and implement it in the interpreter using the literal-capable evaluator with the same subject rules as `input.from`, then implement the `set` inline arm in `SequenceFrame::Advance`, and verify tests for object literals plus expressions, scalar expression, bare scalar literal, `output.as` shaping, `export.as`, opaque input with no expressions, and opaque input with an expression
- [x] 3.2 Add `FrameContext::SelectSwitchDirective` and implement deferred-default case selection in the interpreter (`when` typed slot, boolean result, case/`switch` naming on errors), then implement the `switch` inline arm with transformed input as raw output, and verify tests for first match, fallback, a fallback declared before a matching conditional, no match with task `then`, no match with `continue`, non-boolean `when`, opaque input with a conditional case, default-only switch over opaque input, `input.from`/`output.as`/`export.as`, `if: false`, a switch inside a `do` branch selecting sibling work, and a direct switch branch with `then: end` concluding the workflow
- [x] 3.3 Update the frame/interpreter test fakes for the two new `FrameContext` methods and verify all existing `openworkflow` tests still compile and pass

## 4. Validation

- [x] 4.1 Make `ValidateDocument` scope-aware: build a name set per sequence, reject duplicate names, resolve every task's `then` against that set, treat each fork branch as a one-task scope, and recurse into `do` bodies, and verify tests for valid forward/backward targets, unknown targets, cross-scope targets, duplicates, a branch task targeting a sibling branch (malformed), and a branch `do` targeting its own body (valid)
- [x] 4.2 Validate switch cases: compile every `when` as `kSwitchWhen`, require a non-empty `then` per case, and reject more than one case without `when`, and verify the corresponding malformed-before-submission tests
- [x] 4.3 Validate `set` data through the literal-capable validator so partial wrappers and invalid programs are malformed, and verify the corresponding before-submission tests
- [x] 4.4 Confirm validation reports malformed-document errors (not run failures) for every new static check and that no child is submitted, and verify the validation test group

## 5. Integration, docs, and checks

- [x] 5.1 Add e2e workflows modeled on the reference `set`, explicit-sequence-flow, switch-match, implicit-default, and explicit-default scenarios, including set targets, `then: end`/`then: exit`, and a fork with a `do` branch containing a switch, and verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:openworkflow_e2e_test` passes
- [x] 5.2 Update `docs/testing-openworkflow.md` with runnable `set`, `switch`, and `then` examples matching the implemented strict `when` rule, and verify the documented examples are consistent with the e2e tests
- [x] 5.3 Run the full openworkflow test suite with `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:all //test/openworkflow:all` and verify every target passes
- [x] 5.4 Run `make check` and `make test` and verify the repository-wide checks and tests pass
