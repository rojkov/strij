# Tasks

## 1. Synchronous workflow artifact loader

- [x] 1.1 Add `artifact_loader.{hh,cc}` with `LoadWorkflowArtifact(std::string_view) -> absl::StatusOr<std::string>`: open with `O_RDONLY | O_CLOEXEC | O_NONBLOCK`, `fstat` and require `S_ISREG`, reject artifacts over 4 MiB, read in a loop retrying `EINTR`, and close on every path. Map `ENOENT`/`ENOTDIR` to `NotFound`, non-regular/oversized to `InvalidArgument`, and other failures to `Internal`. Verify `bazel build //src/nodeagent/extensions/task_handlers/openworkflow:openworkflow_task_handler_lib` succeeds.
- [x] 1.2 Add a `artifact_loader_test` target covering a regular file, an empty file, a missing path, a directory (non-regular), a FIFO (must not block), and an oversized file. Verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:artifact_loader_test` passes.
- [x] 1.3 Record the scoped synchronous-I/O exception and its revisit trigger in `AGENTS.md` next to the existing `posix_spawn`/`pipe2`/`pidfd_open` allowances. Verify the text names `openworkflow`'s artifact loader and says it is revisited when the function resolver takes over materialization.

## 2. Invocation input replaces the document body as the seed

- [x] 2.1 Change `WorkflowInterpreter::Start` to accept the invocation input, and seed the data chain as follows: empty body → JSON `null`; non-empty body parsed as JSON or kept opaque; if `document.input.from` exists, evaluate it with the invocation input as subject and use the result as the seed. Verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:interpreter_test` passes.
- [x] 2.2 Update and extend `interpreter_test` with scenarios for body seeding, `input.from` transforming the body, empty body seeding `null` (including as the `input.from` subject), an opaque non-JSON body, and the unchanged child-result chain. Verify the same target passes.
- [x] 2.3 Update the purpose of `openspec/specs/openworkflow-interpreter/spec.md` to describe executing a document loaded from a function artifact with the task body as invocation input. Verify with `openspec show openworkflow-interpreter --type spec` that the stale "runs a workflow task body" wording is gone.

## 3. openworkflow handler loads by function reference

- [x] 3.1 Add `FunctionResolver` to `OpenWorkflowTaskHandler`/factory wiring from `TaskHandlerDeps`. In `HandleTask`, require `parameters[kFunctionParameter]`, resolve it, load the document with the new loader, and only then install the run and call `Start(document, task.body())`. On missing parameter or resolver error send one final `MALFORMED_REQUEST`; on load failure send the loader's mapped status (`NOT_FOUND`/`MALFORMED_REQUEST`/`INTERNAL`). Verify `bazel build //src/nodeagent/extensions/task_handlers/openworkflow:openworkflow_task_handler_lib` succeeds.
- [x] 3.2 Update `openworkflow_task_handler_test` for the new invocation shape: document loaded from a file, body passed as input, missing function, resolver rejection, missing artifact, non-regular artifact, and exactly one final non-OK result on each prelude failure. Verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:openworkflow_task_handler_test` passes.
- [x] 3.3 Update the purpose of `openspec/specs/task-parameters/spec.md` from "which executable to run" to a function artifact. Verify with `openspec show task-parameters --type spec` that the wording no longer implies executables only.

## 4. End-to-end flow and manual procedure

- [x] 4.1 Update `openworkflow_e2e_test` to write the workflow document to a temporary file, pass its path as `parameters["function"]`, and supply the invocation input through the task body instead of the document body. Verify `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:openworkflow_e2e_test` passes.
- [x] 4.2 Rewrite the manual gateway/nodeagent procedure in `docs/testing-openworkflow.md`: store the document at `/tmp/openworkflow-echo.yaml`, invoke it with `x-strij-function` and a JSON body, and explain that the body is the invocation input while `input.from` transforms it. Verify the commands match the invocation shape exercised by the e2e test.

## 5. Verification

- [x] 5.1 Run `make build` and verify all targets compile.
- [x] 5.2 Run `bazel test //test/nodeagent/extensions/task_handlers/openworkflow:all` and verify every openworkflow test passes.
- [x] 5.3 Run `make check` and verify include purity and namespace coherence still pass.
- [x] 5.4 Run `openspec validate openworkflow-function-artifacts --strict` and verify the change validates.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Open the deferred `FunctionResolver` follow-up: widen the contract to a function artifact locator, consider type-aware resolution and materialization, and revisit the synchronous-I/O exception.
