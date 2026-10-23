# Tasks

## 1. Embedded profile and validation

- [x] 1.1 Add the embedded `run.workflow` helpers (namespace sentinel, string-input requirement, inert name/version, `return` rejection, document parse, evaluate-mode check) in a new internal `run_workflow.{hh,cc}`, and add it to `openworkflow_task_handler_lib`; verify with focused unit tests for each accepted and rejected shape and run `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:interpreter_test`.
- [x] 1.2 Extend the pre-execution validation walk with a `RunBody` arm that parses the embedded definition, checks its mode, recursively validates it with a depth guard, and reports failures naming the parent step and embedded `name`; verify with tests for a malformed child, a malformed grandchild, `evaluate.mode: loose`, a skipped `if`, `await: false`, excessive nesting, an external namespace, a declared `return`, and a non-string `input`, then run the interpreter test target.
- [x] 1.3 Keep `container`, `shell`, and `script` run processes rejected as `UNIMPLEMENTED` while allowing the embedded workflow process; verify with one test per unsupported process kind and run the interpreter test target.

## 2. Frame execution

- [x] 2.1 Add the `FrameContext` run effect and the `RunBody` arm in `SequenceFrame::Advance`, following the existing `ExecuteCall` suspension pattern; verify with fake-context frame tests for dispatch and with the existing frame suite via `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:frame_test`.
- [x] 2.2 Implement awaited execution: build the nested input, generate a synthetic child id, register the owner, suspend, start the nested run, handle an inline completion through the existing pending drain, and let the ordinary `output.as`/`export.as`/`then` pipeline consume the child's final value; verify with interpreter tests for ordering, output flow, inline completion, and child-failure status propagation.
- [x] 2.3 Implement detached execution for `await: false`: start the nested run without suspending, complete the step with the transformed input as raw output, apply the ordinary pipeline, and drop later outcomes; verify with interpreter tests for immediate continuation, output shaping, and ignored success/failure.

## 3. Nested runner and lifecycle

- [x] 3.1 Implement the handler-owned nested-workflow runner with start, cancel, ownership of awaited and detached interpreters, a weak parent completion path, a configurable nesting-depth limit (default constant), and recursive subflow support; verify with unit tests covering start, completion, cancel, and recursive nesting, and run `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:openworkflow_task_handler_test`.
- [x] 3.2 Wire the runner through the handler and interpreter constructors so both top-level and nested interpreters can start subflows without changing `HandleTask` or `TaskHandlerDeps`; verify a handler test that executes one embedded subflow and run the handler test target.
- [x] 3.3 Make parent cancellation cancel outstanding awaited nested runs while leaving detached runs alive, and discard every detached outcome; verify with tests that simulate completion, `end`, abandonment, and connection close, then run the interpreter and handler test targets.
- [x] 3.4 Verify lifecycle ownership under recursive embedding and detached runs that outlive the parent handler run; add a regression test that the runner remains valid after the parent is reaped and run the handler test target.

## 4. End-to-end coverage

- [x] 4.1 Add an e2e case with an awaited embedded subflow exercised through the real `piped_executable` handler, passing data via the step's `input.from` and shaping the child result with `output.as`; verify with `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:openworkflow_e2e_test`.
- [x] 4.2 Add an e2e case for `await: false` where the parent continues while the detached child completes, and assert the parent output is the transformed input; verify with the same e2e target.
- [x] 4.3 Add an e2e or handler case for an awaited child failure propagating its status and for a detached child failure being ignored; verify with the same target.

## 5. Documentation and final verification

- [x] 5.1 Document the embedded profile in `docs/testing-openworkflow.md`: `namespace: embedded`, the definition-only `input` string, child input from `input.from`, inert `name`/`version`, `await` on/off, and the unsupported external namespace/`return`; confirm the documented examples are covered by the automated workflow tests.
- [x] 5.2 Run `make test` (or `bazel test //...`) and `make check`, and confirm the new run/embedded tests pass without regressing the openworkflow, parser, or expression suites.
