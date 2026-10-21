# Tasks

## 1. Expression context plumbing

- [x] 1.1 Add an `ExpressionArguments` bundle and a caller-supplied context to `Run`, `Evaluate`, `EvaluateTypedSlot`, and `ValidateTypedSlot`, replacing the hardcoded `{}` seed. Verify `expression_test` covers a non-empty context and `$input`/`$output` remain absent from the wrong slots.
- [x] 1.2 Thread the live context through `ProjectCall` so a `call`'s `with` expressions observe `$context`. Verify `call_strij_test` and `interpreter_test` pass.
- [x] 1.3 Add `TypedSlot::kTaskExport` and the slot-to-variable mapping (`$context`, `$input`, `$output`), and verify compile/run of an export expression in `expression_test`.

## 2. Context state and frame effect

- [x] 2.1 Seed a `utils::Jv` context member with `{}` in `WorkflowInterpreter` and pass it to `if`, `input.from`, task `output.as`, document `input.from`, document `output.as`, and `call` projection. Verify a workflow with no export still observes an empty object.
- [x] 2.2 Add `ExportTaskResult` to `FrameContext`, implement it in the interpreter, and extend the fake context in `frame_test`. Verify the frame suite compiles and passes.
- [x] 2.3 Invoke `ExportTaskResult` after `TransformTaskOutput` on both completion paths — `ApplyOutcome` and `advanceContainer` — before the cursor advances. Verify the existing interpreter/fork suites stay green.

## 3. Export runtime

- [x] 3.1 Evaluate `export.as` against the shaped output with `$input`, `$output`, and `$context`; require a JSON object and replace the context. Verify merge (`$context + .`), replace (`.`), and argument access in `interpreter_test`.
- [x] 3.2 Verify a task without `export`, a skipped task, and a failed child do not change the context. Verify composite `do`/`fork` tasks export their shaped output.
- [x] 3.3 Verify opaque transformed input/output and a non-object export result fail the step with an error naming the step, and that a local child completing inside `Submit` exports exactly once.

## 4. Validation

- [x] 4.1 Add `export.as` to the typed-slot validation pass. Verify a malformed or bare `export.as` fails before any child is submitted.
- [x] 4.2 Track whether the validation walk is inside a fork branch and reject any `export` there with `TASK_STATUS_UNIMPLEMENTED`, including nested `do` and nested forks. Verify the fork task's own `export` is accepted and the rejection happens before submission.

## 5. Integration and docs

- [x] 5.1 Add an e2e workflow that exports a value, reads it in a later `if` or `output.as`, and uses it in the document output. Verify the final result.
- [x] 5.2 Update `docs/testing-openworkflow.md` with an export example and the fork-branch restriction, and verify the examples match the tests.
- [x] 5.3 Run `make test` and `make check`; verify the full suite and include/namespace checks pass.
