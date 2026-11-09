# Proposal

## Why

Strij can run arbitrary executables and Open Workflow documents, but it has no first-class Python execution path and no way for Python code to fan work out across the cluster. A `pyfunction` task handler with a persistent Python worker pool, plus a small `strij` Python SDK, gives users Ray-like remote calls while reusing the existing child-task routing, admission, and gateway-forwarding machinery.

## What Changes

- Add a bundled `pyfunction` task handler and factory. `capacity.concurrency` defines the number of Python worker processes and the maximum number of in-flight `pyfunction` tasks; every admitted task owns exactly one worker slot and the handler keeps no queue.
- Spawn one Python worker process per slot with `posix_spawn`, communicate over dedicated pipes with a length-prefixed protobuf protocol, send worker stdout/stderr to node logs, detect exit with a pidfd, and respawn on crash. In-flight tasks fail with `INTERNAL`; outstanding remote calls are dropped.
- Support two invocation modes on the same task type:
  - Reference/JSON mode when `Task.parameters["function"]` is present: `embedded:<entry>` reads `python_code` from a JSON body, `module:<module>:<qualname>` imports a function; the result is JSON.
  - Pickle mode when `function` is absent: the body is a versioned cloudpickle `Invocation`; the result is a pickled Python value.
- Add a `strij` Python package with a worker runtime and a client runtime. The same `@strij.remote` decorator works in both: inside a worker it submits child tasks through the pipe, and in a client it submits over the gateway HTTP endpoint.
- Resolve `RemoteRef` arguments eagerly before serialization, and reject refs the resolver cannot reach with a clear error.
- Map failures onto `TaskStatus`: malformed invocation to `MALFORMED_REQUEST`, Python exception to `INTERNAL`, no ready worker to `UNAVAILABLE`, timeout to `DEADLINE_EXCEEDED`, admission or no eligible node to `CAPACITY_REFUSED`.
- Build the worker with Bazel (`py_library`/`py_binary`) and distribute the client as a normal Python package (`pyproject.toml`), with `cloudpickle` and a `protobuf` runtime as Python dependencies. The generated worker protocol modules require the protobuf runtime; a future C++ worker linked against libpython would decode the same frames with the C++ protobuf library and drop the Python protobuf dependency.
- The reference/JSON mode is reachable through the existing `Task.parameters` map, so callers such as OpenWorkflow can submit it via `parameters.function` without a Python client or any interpreter change.

Out of scope: an object store or non-blocking object refs, cancellation of running Python, automatic retries, multi-return and streaming results, non-Python SDKs, and any OpenWorkflow projection beyond the existing `parameters` map.

## Capabilities

### New Capabilities

- `pyfunction-task-handler`: the `pyfunction` task handler, worker slot lifecycle, process transport, invocation modes, timeouts, error mapping, and configuration.
- `pyfunction-sdk`: the `strij` Python package, worker and client runtimes, `@strij.remote` calls, `RemoteRef` resolution, and the client submission contract.

### Modified Capabilities

None.

## Impact

- New C++ extension under `src/nodeagent/extensions/task_handlers/pyfunction/`, linked into the bundled nodeagent extensions.
- New protobuf config and pipe-protocol definitions under `api/nodeagent/extensions/task_handlers/pyfunction/`.
- New Python package under `python/strij/` with a `pyproject.toml`, Bazel `py_library`/`py_binary` targets, a vendored cloudpickle dependency, and a `protobuf` Python runtime pinned to the Bazel protobuf version (33.4).
- The nodeagent binary carries the worker as a `data` dependency and resolves it through runfiles; operators can instead point the handler at an installed `strij.worker` module.
- No changes to the gateway wire protocol, scheduler contracts, admission interfaces, or existing task handlers.
- Tests: Python unit tests for the SDK, C++ unit tests for framing and slot management with a fake worker, and real-Python cluster e2e coverage (skipped with a clear message when Python, cloudpickle, or the protobuf runtime is unavailable).
