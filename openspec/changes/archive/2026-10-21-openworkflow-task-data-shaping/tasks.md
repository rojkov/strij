# Tasks

## 1. Expression engine foundations

- [x] 1.1 Add typed-slot validation to `expression.{hh,cc}`: a top-level string must be a whole `${ }` expression, object-form members must all be wrapped, and every discovered program is compiled. Verify with `expression_test` cases for bare `if`/`from`/`as`, partial wrappers, nested members, and valid wrapped forms.
- [x] 1.2 Add the JSON-faithful typed-slot evaluation helper (result serialized via `ValueToJv(...).Dump()`) and per-slot variable sets (`$context` for `if`/`input.from`/document output, plus `$input`/`$output` for task `output.as`). Verify with `expression_test` cases covering a string result, `$input`/`$output`, and an unbound descriptor argument.
- [x] 1.3 Switch document `input.from` seeding to the JSON-faithful helper and verify a string result becomes JSON `"world"` rather than opaque bytes in `interpreter_test`.

## 2. Frame and context plumbing

- [x] 2.1 Extend `FrameContext` with `EvaluateCondition`, `TransformTaskInput`, and `TransformTaskOutput`; implement them in `WorkflowInterpreter` over the expression engine. Verify the fake context in `frame_test` compiles and the frame tests pass.
- [x] 2.2 Thread the transformed input through `ExecuteCall` and `materializeChild`: add `step_input_` to `SequenceFrame`, set it before dispatch, and pass `FrameContext&` into `ApplyOutcome`. Verify the existing interpreter and frame suites stay green (`bazel test //test/nodeagent/extensions/task_handlers/openworkflow:all`).

## 3. Task input.from

- [x] 3.1 Make `input.from` the subject for a `call`'s `with` expressions and the source of the child body, evaluated once per step. Verify a parameter expression reads from the transformed payload instead of the raw input.
- [x] 3.2 Seed nested `do` and `fork` sequences from `input.from`. Verify a nested step and a fork branch both observe the transformed value.
- [x] 3.3 Make the `call: strij` child body JSON-faithful: a string transform yields `"world"` and opaque input passes as bytes. Update `call_strij_test`/`openworkflow_e2e_test` expectations and verify.
- [x] 3.4 Add `input.from` to the pre-execution validation pass, including the wrapped-only rule. Verify a malformed or bare `input.from` fails before any submission.

## 4. Task if

- [x] 4.1 Evaluate `if` at step entry against the raw input, require a boolean, and on `false` short-circuit body, `then`, `output.as`, and `export` while leaving the data value unchanged. Verify true, false, skipped-chain, and nested/fork-branch cases in `interpreter_test`.
- [x] 4.2 Report a non-boolean condition and a non-JSON subject as run failures naming the step, and a malformed/bare `if` as a pre-execution malformed document. Verify with `interpreter_test`.

## 5. Task output.as

- [x] 5.1 Apply `output.as` after a `call` completes, on both the resumed-result path and the inline local-completion path, exactly once. Verify shaped data for the following step and no double application.
- [x] 5.2 Apply `output.as` to a `do` sequence's final value and a `fork` join value. Verify shaped values continue the chain.
- [x] 5.3 Bind `$input` and `$output` in `output.as` and fail the step when the transformed input or raw output is opaque. Verify the subject/input error messages name the step.
- [x] 5.4 Add `output.as` to the pre-execution validation pass. Verify a malformed or bare `output.as` fails before any submission.

## 6. Document output.as

- [x] 6.1 Apply document `output.as` exactly once at root `Completed`, using the final data value as subject and `$context` as empty. Verify shaped final result bodies and the no-`output.as` path.
- [x] 6.2 Report a non-JSON final value as a run failure and a malformed/bare document `output.as` as a pre-execution malformed document. Verify in `interpreter_test`.

## 7. Integration and docs

- [x] 7.1 Add an e2e workflow through the real `piped_executable` handler that combines `input.from`, an `if` skip, and `output.as`, and verify the final result.
- [x] 7.2 Update `docs/testing-openworkflow.md` for the wrapped-only typed slots and JSON-quoted string bodies, and verify the documented examples match the tests.
- [x] 7.3 Run `make test` and `make check`; verify the full suite and include/namespace checks pass.
