# Tasks

## 1. Baseline

The test tree was verified restored at planning time: `git status --short` is clean, 137 files are tracked under `test/`, and `test/nodeagent/extensions/task_handlers/openworkflow/` holds `expression_test.cc` and `interpreter_test.cc` on the pre-change `TypedSlot` API. No `git checkout -- test` step is required.

- [x] 1.1 Run `bazel test //test/nodeagent/extensions/task_handlers/openworkflow/...` and verify the pre-change expression and interpreter suites pass, so later failures are attributable to this change

## 2. Expression engine: one slot descriptor, one path

- [x] 2.1 Add `ExpressionSlot` (display name, `SlotVariables`, literal policy) and the slot constants from `design.md` D1; delete `TypedSlot`, `TypedSlotName`, and `typedSlotReservedNames`; verify `bazel build //src/nodeagent/extensions/task_handlers/openworkflow:openworkflow_task_handler_lib` succeeds
- [x] 2.2 Collapse `ValidateTypedSlot`/`Validate` and `EvaluateTypedSlot`/`Evaluate` into `Validate(value, slot, arguments)` and `Evaluate(value, slot, subject, arguments)`; keep the literal policy at its current value for every slot so behavior is unchanged; verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:expression_test //test/nodeagent/extensions/task_handlers/openworkflow:interpreter_test` passes with the pre-existing assertions
- [x] 2.3 Move all ten `interpreter.cc` slot call sites (validation and evaluation) to the new API, and verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow/...` still passes
- [x] 2.4 Update `test/.../openworkflow/expression_test.cc` for the new API: descriptor-based slot constants, literal policy per family, and slot-named error messages; verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:expression_test` passes

## 3. Value-construction slots accept literals

- [x] 3.1 Set `literals_ = true` on `kInputFrom`, `kTaskOutput`, `kTaskExport`, and `kDocumentOutput`; verify `bazel build //src/nodeagent/extensions/task_handlers/openworkflow:openworkflow_task_handler_lib` succeeds
- [x] 3.2 Add `interpreter_test` coverage for literal members, a literal root string producing a quoted JSON string, a mixed literal/expression object, and the partial-wrapper and malformed-program errors; verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:interpreter_test` passes
- [x] 3.3 Add `expression_test` coverage that an expression-only slot still rejects a bare string while a value-construction slot accepts it, and verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:expression_test` passes
- [x] 3.4 Note in `docs/testing-openworkflow.md` that a root literal string body is JSON-quoted, and verify the documented `openworkflow-pyfunction` example runs to `42` with `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:openworkflow_e2e_test` or the documented manual `curl`

## 4. Conditional JSON subject for output transformation and export

- [x] 4.1 Apply the literal-only rule in `transformOutput` and `ExportTaskResult` so the JSON precondition is checked only when the slot tree contains an expression, reusing `CollectExpressions`; verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:interpreter_test` passes
- [x] 4.2 Add `interpreter_test` cases for a literal-only `output.as` and `export.as` over opaque values, plus the retained failure for an expression-bearing slot over an opaque value; verify the same target passes
- [x] 4.3 Add a `step_data_value`- or handler-level case for a literal-only document `output.as` over an opaque final value, and verify the owning target passes

## 5. Integration verification

- [x] 5.1 Run `make format` then `make check_format`, and verify no Open Workflow file is reformatted
- [x] 5.2 Run `make check` and `make test` against the restored tree, and verify both succeed
- [x] 5.3 Run `openspec validate "openworkflow-literal-value-slots" --strict` and verify the change is valid

## Workflow follow-up

- Archive the change once review is complete and implementation has landed.
