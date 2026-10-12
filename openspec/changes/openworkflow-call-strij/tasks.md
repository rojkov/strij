# Tasks

## 1. Value ↔ JSON bridge

- [ ] 1.1 Add `ValueToJv`/`JvToValue` in `src/openworkflow/value_jv.{hh,cc}` covering all seven `Value::Data` alternatives including `monostate` → JSON `null`; verify with a round-trip test per alternative plus mixed nested object/array cases in `test/openworkflow/value_jv_test.cc`
- [ ] 1.2 Verify the bridge is lossless for `int64_t` at the extremes (`INT64_MIN`, `INT64_MAX`) and that a `double` distinguishing `1` from `1.0` survives; verify as a focused test case in `value_jv_test.cc`
- [ ] 1.3 Add the new library to `src/openworkflow/BUILD.bazel` and the test to `test/openworkflow/BUILD.bazel`; verify `bazel test //test/openworkflow:all` passes

## 2. Strict-mode expression recognition (in the handler package)

- [ ] 2.1 Implement a recursive scan over a `Value` tree that identifies a string as an expression if and only if it is entirely wrapped in `${ }`, descending into object members and array elements; verify a wrapped string nested two levels deep is found, an unwrapped string at any depth is not, and numbers/booleans/null are never candidates
- [ ] 2.2 Reject a string that contains `${ }` but has literal text outside the wrapper, such as `"jobs/\(.id)"`, with an error stating the wrapper must span the whole value; verify the check is applied at every depth, not just the top level
- [ ] 2.3 Read `evaluate.mode`, defaulting to `strict` when the `evaluate` section or its mode is absent; verify a document declaring `loose` fails with an error stating the mode is unsupported
- [ ] 2.4 Replace each identified expression in the tree with its evaluated result, preserving the surrounding structure and the kinds of non-expression values; verify a mixed object yields the evaluated member beside the literal one

## 3. Data chain and expression subject (in the handler package)

- [ ] 3.1 Define the data value as a variant of JSON and opaque bytes, with a sniff-on-construct helper using `utils::Jv::Parse`; verify JSON text, non-JSON text, and empty input each classify correctly
- [ ] 3.2 Bind the data chain as the jq expression subject at every step, and bind the DSL's `$context` separately to an empty JSON object; verify a `${ .field }` expression reads the data chain while a `${ $context.field }` expression reads the empty object, never the chain
- [ ] 3.3 Reject a `${ }` expression whose subject is not JSON, with an error naming the step and stating the subject is not JSON; verify the error is distinct from a syntax error in the same position

## 4. Interpreter core: resumable step loop (in the handler package)

- [ ] 4.1 Define the interpreter in `src/nodeagent/extensions/task_handlers/openworkflow/` under `namespace strij::nodeagent::task_handlers::openworkflow`, with a step index and an explicit waiting state, holding the `ChildTaskSubmitter` received at factory construction; verify it compiles in that package and that `make check_namespaces` passes
- [ ] 4.2 Implement a loop that advances one step at a time and suspends on a child submission, resuming at the following step; verify a three-step body runs in declaration order
- [ ] 4.3 Implement the reentrancy guard so a child completing *inside* `Submit` is recorded rather than re-entering the loop; verify a locally admitted child yields exactly one step advance and no deadlock
- [ ] 4.4 Match a resuming result by child task id and ignore a result for an unknown id; verify a stale result is dropped and the loop stays suspended
- [ ] 4.5 Deliver exactly one final result on success carrying the workflow output, and report the failing step by name on failure with no further steps executed; verify both paths, and that a failure after a suspension reports once rather than waiting indefinitely

## 5. Two-phase validation

- [ ] 5.1 Add a validation pass over the body covering call targets, the `with` key set, requirement coercion, and compiling every string the recursive strict-mode scan identifies as an expression, returning before any submission; verify an unknown key, a bad requirement amount, a partially-wrapped string, and a malformed `${ }` nested two levels deep each fail with no child submitted
- [ ] 5.2 Make validation errors distinguishable from execution errors, so a report can state whether the document is malformed or the run was given the wrong data; verify one test asserting the two error kinds are not conflated
- [ ] 5.3 Verify a structurally valid document with a data-dependent failure passes validation and fails during execution instead

## 6. `call: strij` projection (in the handler package)

- [ ] 6.1 Accept exactly `type`, `parameters`, `requirements` under `with`, rejecting any other key with an error naming the offending key and listing the three accepted; verify each accepted key projects onto the child task and `Task.parameters` is byte-for-byte what was declared
- [ ] 6.2 Require `type`, rejecting a `call: strij` that omits it; verify the error states `type` is required and no child is submitted
- [ ] 6.3 Refuse expressions in `type` and in `requirements` amounts, with an error saying so per position; verify each position independently
- [ ] 6.4 Admit a whole-value expression in a `parameters` value, evaluating with the data chain as subject and rendering the result as the string parameter; verify a string result is unquoted, a numeric result renders canonically, and a `null` result is rejected
- [ ] 6.5 Reject an object or array result from a `parameters` expression as non-scalar; verify no child is submitted
- [ ] 6.6 Coerce `requirements` amounts strictly: accept non-negative integers, and reject fractional, negative, out-of-range, non-numeric, and unit-suffixed values such as `100M` with an error naming the requirement and the value; verify each rejection independently, and specifically that `100M` is rejected rather than truncating to `100`
- [ ] 6.7 Recognize the reserved call targets `http`, `openapi`, `asyncapi`, `grpc`, `mcp` and report each as recognized-but-unsupported, distinctly from an unrecognized target and from a declared-function name; verify all three error kinds are distinguishable

## 7. Nodeagent handler

- [ ] 7.1 Add `api/nodeagent/task_handlers/openworkflow/config.proto` selecting an evaluator by name, and wire it into the default nodeagent config; verify the nodeagent builds and loads with the handler enabled
- [ ] 7.2 Implement the handler factory: retrieve `ChildTaskSubmitter` from `TaskHandlerDeps`, resolve the evaluator through the shared loader, and register as `"openworkflow"`; verify the registry lookup returns the factory
- [ ] 7.3 Implement `HandleTask` as a thin adapter over the interpreter — parse the body, hand off, retain the `ResultSender` — with no logic of its own; verify `HandleTask`'s signature is unchanged
- [ ] 7.4 Register the handler in `nodeagent_framework.cc`; verify `bazel build //...` succeeds and the nodeagent starts with the handler present
- [ ] 7.5 Honor `RegisterOnClose` to stop the workflow when the originating connection tears down, abandoning outstanding children; verify a teardown mid-suspension delivers no further result and does not resume
- [ ] 7.6 Delete the superseded `src/nodeagent/extensions/task_handlers/workflow/` handler and its tests, and remove `workflow` from any bundled nodeagent config; verify `bazel build //...` succeeds and no reference to the deleted task type remains
- [ ] 7.7 Report workflow failure as a final `TaskResult` with a non-OK `status` and the failing step's message in `body`, sent through `ResultSender::Send`, using the codes established by the landed `task-result-status` change: `TASK_STATUS_MALFORMED_REQUEST` for an unparsable body or a failed pre-execution validation, `TASK_STATUS_UNIMPLEMENTED` for a recognized-but-unsupported call target, `TASK_STATUS_INTERNAL` when no more specific cause applies, and a failed child's code propagated as-is; verify each classification is exercised by a test, and that the handler never calls `DeliverError`, which is not on its `ResultSender` path

## 8. Integration and verification

- [ ] 8.1 Add an end-to-end test submitting a workflow whose single `call: strij` step invokes `piped_executable`, and assert the child's output becomes the workflow's final result; verify `bazel test //test/nodeagent/...` passes
- [ ] 8.2 Add an end-to-end test for a child refusal surfacing as a workflow failure naming the step; verify the failure is reported exactly once
- [ ] 8.3 Run `make test` and `make check`; verify all pass
- [ ] 8.4 Update `AGENTS.md` with the `openworkflow` handler, the interpreter's location beside it, the `Value` ⇄ JSON bridge as the only addition to the SDK, and the `call: strij` shape; verify `make check_namespaces` and `make check_includes` still pass
- [ ] 8.5 Confirm `test/consumer/` still builds, verifying the added `src/openworkflow/` surface does not leak `absl`, `Jv`, or child-submission types into the external-module path; verify `make test_consumer` passes
