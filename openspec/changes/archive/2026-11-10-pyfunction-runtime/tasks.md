# Tasks

## 1. Build and dependency scaffolding

- [x] 1.1 Vendor cloudpickle under `third_party/python/cloudpickle/` with its license and a Bazel `py_library`; verify `bazel build //third_party/python/cloudpickle:cloudpickle_lib`.
- [x] 1.2 Create the `python/strij/` package skeleton (`__init__.py`, `_invocation.py`, `_runtime.py`, `_worker.py`) and `pyproject.toml` declaring `cloudpickle` and `protobuf` (pinned to the Bazel module's Python runtime version); verify `PYTHONPATH=python python3 -c "import strij"`.
- [x] 1.3 Add Bazel `py_library` and worker `py_binary` targets for `strij` depending on `@protobuf//:protobuf_python`, plus the `api/nodeagent/extensions/task_handlers/pyfunction` proto package generated with `py_proto_library`; verify both targets build.
- [x] 1.4 Define `PyFunctionTaskHandlerConfig` and the pipe-protocol messages in `api/nodeagent/extensions/task_handlers/pyfunction/`; verify the generated C++ and Python proto targets build and that the worker target imports the generated modules with the Bazel-provided protobuf runtime.
- [x] 1.5 Verify the worker packaging carries the protobuf runtime through runfiles so it starts with no host `protobuf` installed; document that a future C++ worker linked against libpython decodes the same frames with the C++ protobuf library and drops this Python dependency.

## 2. Worker protocol and bootstrap

- [x] 2.1 Implement C++ length-prefixed frame encoding/decoding with a configured maximum; add unit tests for round-trip, partial frames, and oversized lengths under `test/nodeagent/extensions/task_handlers/pyfunction/`.
- [x] 2.2 Implement Python frame read/write using the generated protobuf modules and the worker bootstrap (`Hello`/`Ready`, dispatch loop, `sys.stdout` redirect, `Shutdown`); verify with a Python unit test driving a fake handler pipe.
- [x] 2.3 Implement the worker process wrapper (`posix_spawn`, dedicated protocol pipes, stdout/stderr capture, pidfd poll, nonblocking drain, single in-flight write queue); verify with a C++ fake-worker helper that covers exit and stderr.
- [x] 2.4 Implement the protocol version handshake and termination on mismatch or oversized frames; verify both rejection paths in unit tests.

## 3. Handler core and slot lifecycle

- [x] 3.1 Implement the `pyfunction` factory registration and configuration validation (`concurrency >= 1`, worker target present, non-negative durations, non-zero frame bound); verify valid and invalid configurations in tests.
- [x] 3.2 Implement the slot state machine (starting/idle/running/dead), assignment without a queue, starting-holds-task, and `UNAVAILABLE` when no worker can be assigned; verify with the fake worker.
- [x] 3.3 Enforce exactly one terminal `TaskResult` per task and release the sender/admission scope on success, failure, and cancellation; verify with a recording sender.
- [x] 3.4 Implement eager worker spawn per slot, the startup timeout, and respawn after startup failure; verify startup and recovery paths.

## 4. Invocation modes

- [x] 4.1 Implement reference/JSON mode dispatch (`embedded:<entry>` and `module:<module>:<qualname>`), `python_code` extraction, argument binding (`args`/`kwargs`, then `input`, then whole body), and JSON results; verify each binding rule in Python tests.
- [x] 4.2 Implement pickle mode with the versioned `Invocation` envelope and pickled results; verify valid, unsupported-version, and unpicklable-body cases.
- [x] 4.3 Implement the reference/pickle failure mapping (`MALFORMED_REQUEST`, `NOT_FOUND`, `INTERNAL`, non-JSON result); verify each status in tests.
- [x] 4.4 Document the task contract and both invocation modes in `docs/pyfunction.md`; verify the documented examples match the tests.

## 5. Remote calls and eager refs

- [x] 5.1 Emit `RemoteCall` frames from the worker runtime with serialized writes, and submit them from the handler through `ChildTaskSubmitter`; verify a fake submitter receives the child type, body, and requirements.
- [x] 5.2 Deliver child outcomes as `RemoteResult` frames and resolve the matching worker ref; verify success, chunked, and error paths in Python tests.
- [x] 5.3 Implement eager `RemoteRef` resolution in the SDK, including recursive containers, foreign-ref rejection, and returning-ref rejection; verify each case in Python tests.
- [x] 5.4 Implement the remote-call timeout and `RemoteError` status mapping; verify timeout and rejection paths.

## 6. Lifecycle failure policies

- [x] 6.1 Implement the task timeout (kill worker, `DEADLINE_EXCEEDED`) and client-disconnect handling (kill worker, drop result, free slot, unregister callback); verify both with the fake worker.
- [x] 6.2 Implement crash handling (fail in-flight with `INTERNAL`, drop outstanding receivers, respawn, no retry); verify the sequence in tests.
- [x] 6.3 Add a nodeagent configuration example at `config/examples/nodeagent-pyfunction.yaml` with timeouts and worker settings; verify `nodeagent --config_file ... --validate_only` accepts it.
- [x] 6.4 Document timeout, crash, and disconnect behavior in `docs/pyfunction.md`; verify it matches the implemented statuses.

## 7. SDK worker runtime

- [x] 7.1 Implement the `strij` surface (`init`, `remote`, `RemoteRef`, `RemoteError`) and runtime selection with a clear missing-runtime error; verify in Python unit tests.
- [x] 7.2 Implement the worker pipe transport and ref resolution, including thread-safe submissions; verify concurrent submissions and awaited results in tests.
- [x] 7.3 Implement invocation serialization and the eager-ref resolver integration in the submission path; verify envelope versioning and ref resolution end to end in Python tests.
- [x] 7.4 Document the SDK API, limits, and worker-side behavior in `docs/pyfunction.md`; verify the documented snippets run against the tests.

## 8. SDK client runtime

- [x] 8.1 Implement the HTTP client runtime (`strij.init(gateway_url)`, POST `/tasks/pyfunction`, non-blocking ref creation, `.get()` decoding, HTTP-to-`TaskStatus` mapping); verify against a local mock HTTP server.
- [x] 8.2 Implement client-side ref resolution and the timeout/error distinctions; verify resolution, timeout, and non-2xx paths.
- [x] 8.3 Document client setup and gateway URL configuration in `docs/pyfunction.md`; verify the documented example works against the mock server.

## 9. Integration and end-to-end coverage

- [x] 9.1 Link the handler into `bundled_extensions` and add the worker `py_binary`, its generated `*_pb2` modules, and `@protobuf//:protobuf_python` as `data` dependencies of the nodeagent binary; verify `bazel build //src/nodeagent/exe:nodeagent` and that the e2e runfiles lookup finds the worker and its protobuf runtime.
- [x] 9.2 Add a real-Python e2e test for client pickle-mode submission through a gateway, covering success and task failure; verify with `bazel test` on a Python-capable host.
- [x] 9.3 Add a real-Python e2e test for reference/JSON mode using `parameters.function` with embedded `python_code`; verify the JSON result is returned.
- [x] 9.4 Add e2e coverage for a nested remote call (local and forwarded), capacity exhaustion, and worker crash; verify the expected statuses and that no task hangs.
- [x] 9.5 Add a capability probe that skips the real-Python e2e suite with an explanatory message when Python, cloudpickle, or protobuf is unavailable; verify the skip path.
- [x] 9.6 Document operator deployment (runfiles worker vs installed `worker_module`, capability-aware routing, single-node nesting headroom) in `docs/pyfunction.md`; verify the commands in the documentation.

## 10. Final verification

- [x] 10.1 Run `make build` and `make test`; verify all targets pass.
- [x] 10.2 Run `make check` and `make check_format`; verify both are clean.
- [x] 10.3 Run `make test_asan` and `make test_tsan` on `//src/...`; verify the C++ suites pass and the Python worker is not instrumented.
- [x] 10.4 Run `make test_consumer`; verify the external-module smoke test is unaffected.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived specs and the completed task list after archiving.
