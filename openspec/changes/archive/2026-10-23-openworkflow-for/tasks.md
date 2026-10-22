# Tasks

## 1. Expression local variables

- [x] 1.1 Extend `ExpressionArguments` with a lexical-binding view (name plus `const utils::Jv*`) and thread it through `ExpressionEngine::Run`, `Evaluate`, `EvaluateTypedSlot`, and the internal compile/arg-building helpers so locals are declared after the reserved variables and deduplicated innermost-first; add expression tests proving a local is visible to `${ $item }` and absent when not passed, and run `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:expression_test`.
- [x] 1.2 Add `TypedSlot::kForIn` and `TypedSlot::kForWhile` with their names and variable sets (`$context` plus locals, no `$input`/`$output`), and add `ValidateTypedSlot`/`EvaluateTypedSlot` coverage proving wrapped-only acceptance, syntax errors, and boolean/non-boolean results; run the same expression test target.
- [x] 1.3 Add a small loop-variable normalization helper (trim, default `item`/`index`, strip one leading `$`, validate `[A-Za-z_][A-Za-z0-9_]*`, reject `each == at` and `context`/`input`/`output` collisions) with unit tests for each rejection and for both `color` and `$color` spellings; run the expression or interpreter test target that hosts the helper.

## 2. Frame layer

- [x] 2.1 Add `LexicalScope` (parent plus `each`/`at` names and `utils::Jv` values), give `SequenceFrame` a `std::shared_ptr<const LexicalScope>`, and propagate it when materializing `do`, `fork`, and `for` frames; verify existing frame tests still pass with `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:frame_test`.
- [x] 2.2 Add the `FrameContext` effects for `for.in` and `for.while` evaluation with the calling scope, and implement `ForFrame` as the lazy, resumable state machine from design D5 (evaluate `in` once, create one body sequence per iteration, seed from the accumulator, check `while` after completion, handle `Suspended`/`Failed`/`Ended`/`Completed`, keep a failed body alive, abandon the current body); verify with frame tests for ordering, empty/null collections, accumulator chaining, suspension/resumption, `while` stop, `end`, failure, abandonment, and the synchronous iteration bound.
- [x] 2.3 Integrate `ForBody` into `SequenceFrame::materializeChild`/`advanceContainer` and pass the loop body's sequence scope so nested `do`/`fork`/`for` inherit it; add a frame test proving a nested `do` inside a `for` body still sees `$item`; run `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:frame_test`.

## 3. Interpreter

- [x] 3.1 Implement `EvaluateForIn` (string form through `kForIn`, non-string through the literal-capable path; array iterates, `null` is empty, any other result is an `INTERNAL` failure naming the task; opaque subject errors) and `EvaluateForWhile` (accumulator subject, current loop variables and `$context`, boolean required); add interpreter tests for each outcome and run `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:interpreter_test`.
- [x] 3.2 Thread the current lexical scope through `EvaluateCondition`, `TransformTaskInput`, `TransformTaskOutput`, `ExportTaskResult`, `EvaluateSet`, `SelectSwitchDirective`, `ExecuteCall`, and `ProjectCall` so body expressions (`if`, `input.from`, `set`, call `with`, `switch.when`, `output.as`, `export.as`) resolve `$item`/`$index`; add interpreter tests for a set-based loop, a call-based loop suspended across a result, and nested loops with shadowing; run the interpreter test target.
- [x] 3.3 Make validation scope-aware: validate `for.in`/`while`, run loop-variable normalization checks, recurse into each `for` body with its loop names visible, validate the body as its own sequence scope, and propagate `inside_fork_branch` into the body; add validation tests for bare `in`, bare `while`, non-array inline `in`, invalid/colliding names, malformed body tasks, and a body expression that legitimately reads `$item`; run the interpreter test target.
- [x] 3.4 Wire failure attribution and the synchronous iteration bound: `in`/`while` failures carry `INTERNAL` plus the `for` task name, body failures propagate their existing origin, and a non-suspending loop exceeding the bound fails with `INTERNAL`; add tests for both failure paths and for a loop that suspends on each iteration without tripping the bound; run the interpreter test target.

## 4. End-to-end coverage

- [x] 4.1 Add an `openworkflow_e2e_test` case that runs a `for` over a JSON collection with the real `piped_executable` handler, accumulates results across iterations, and asserts the final body; include a nested `for` or a `for` inside a fork branch to exercise scope inheritance; run `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:openworkflow_e2e_test`.
- [x] 4.2 Add an e2e or interpreter case adapted from the reference `for` conformance scenario (`colors`/`indexes`) using the strict `${ }` spelling, and assert the exact accumulated output; verify with the same e2e or interpreter target.

## 5. Documentation and final verification

- [x] 5.1 Document `for` in `docs/testing-openworkflow.md`: strict `in`/`while` spelling, default and custom loop variables, accumulator behavior, nested loops, and the `$input` limitation; verify the examples parse/run with the automated workflow tests.
- [x] 5.2 Run `make test` (or `bazel test //...`) and `make check`, and confirm the new tests pass without regressing the existing openworkflow, parser, or expression suites.
