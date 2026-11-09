# PyFunction task handler and Python SDK

`pyfunction` runs Python functions as Strij tasks in persistent worker
processes and exposes a small `strij` Python package for remote calls.

## Task contract

Submit a task of type `pyfunction`. The handler selects one of two invocation
modes from the task parameters:

| `Task.parameters["function"]` | Mode | Body | Result |
|---|---|---|---|
| present | reference / JSON | JSON object | JSON |
| absent | pickle | cloudpickle `Invocation` | cloudpickle value |

The task status maps failures as follows:

| Condition | `TaskStatus` |
|---|---|
| malformed body, bad envelope version, mixed JSON binding | `MALFORMED_REQUEST` |
| unresolvable module or entrypoint | `NOT_FOUND` |
| Python exception or non-JSON result | `INTERNAL` |
| no assignable worker | `UNAVAILABLE` |
| task timeout or startup timeout | `DEADLINE_EXCEEDED` |
| child admission / no eligible node for a nested call | `CAPACITY_REFUSED` |
| worker protocol/version failure or crash | `INTERNAL` |

### Reference / JSON mode

`function` is either `embedded:<entry>` or `module:<module>:<qualname>`.

An embedded task supplies Python source in `python_code`:

```json
{
  "python_code": "def main(value):\n    return value * 2",
  "input": 21
}
```

with `function = "embedded:main"`; the result is `42`.

A module task imports the callable, for example
`function = "module:example.tasks:main"`.

Arguments bind from the body after removing `python_code`:

1. `args` and/or `kwargs` are passed through, or
2. `input` is the single positional argument, or
3. the whole remaining body is the single positional argument.

Supplying `input` together with `args` or `kwargs` is `MALFORMED_REQUEST`.

Callers such as OpenWorkflow need no Python client: they populate
`Task.parameters["function"]` and send the JSON body.

### Pickle mode

The body is a versioned cloudpickle envelope:

```python
{
    "__strij_invocation__": 1,
    "function": callable,
    "args": (1, 2),
    "kwargs": {},
}
```

Unsupported versions and unpicklable bodies are `MALFORMED_REQUEST`. The
`strij` SDK produces this envelope automatically.

## Python SDK

Install the client package from `python/` (Python >= 3.10):

```bash
pip install ./python
```

The client and the worker must run the same Python **minor** version (for
example both 3.11). Cloudpickle serializes Python code objects, which are not
portable across interpreter versions; a mismatch can crash the worker while it
unpickles the invocation. Recent clients include their Python version in the
request so the worker rejects a mismatch with a clear `MALFORMED_REQUEST`
instead of exiting silently.

### Troubleshooting a client/worker Python mismatch

Clients may report `MALFORMED_REQUEST` if the host `python3`
does not match the worker's Python. Then run the client script
through the interpreter bundled with the Bazel worker:

```bash
bazel build //src/nodeagent/exe:nodeagent //python:worker

RF="$(bazel info bazel-bin)/python/worker.runfiles"
PY311=$(echo "$RF"/rules_python++python+python_3_11_*/bin/python3)

PYTHONPATH="$RF/_main/python:$RF/_main:$RF/_main/third_party/python:$RF/protobuf+/python" \
  "$PY311" test.py
```

Adjust the `python_3_11_*` glob if the worker was built with another Python
minor version.

```python
import strij

strij.init("http://gateway:8080")


@strij.remote
def add(left, right):
    return left + right


@strij.remote(requirements={"cpu": 2}, timeout=60)
def work():
    return "done"


ref = add.remote(2, 5)      # returns immediately
print(ref.get())            # 7
print(work.remote().get())  # "done"
```

`@strij.remote` keeps the normal call local; `.remote(*args, **kwargs)`
submits and returns a `RemoteRef`. `.get(timeout=None)` is idempotent. A
timeout raises `RemoteTimeoutError` without cancelling the remote task. A
remote failure raises `RemoteError`, whose `.status` is a `TaskStatus` and whose
message preserves the remote traceback.

Requirements become `X-Strij-Resources-<pool>` headers for the gateway, which
resolves them into `Task.requirements`.

### Eager references

Before serialization, the runtime recursively resolves `RemoteRef` values in
positional and keyword arguments, including inside lists, tuples, and
dictionaries. A ref from another runtime raises before submission. Returning a
ref from a task fails with a clear error; call `.get()` in the task instead.

### Worker runtime

Inside a worker the same decorator uses the pipe-backed runtime installed by
the bootstrap. Frame writes are serialized, so user threads may submit
concurrently; results resolve on the thread awaiting the ref.

## Lifecycle behavior

- **One worker per slot.** `capacity.concurrency` is both the worker count and
  the in-flight task limit. There is no handler queue: with no assignable
  worker the task receives `UNAVAILABLE`.
- **Startup.** Workers spawn eagerly. A task assigned while the worker is
  starting stays in that slot until the readiness handshake. A worker that does
  not become ready within `startup_timeout` is killed and the task receives
  `DEADLINE_EXCEEDED`; the slot is restarted.
- **Task timeout.** `task_timeout` (default `0`, disabled) kills the worker and
  delivers `DEADLINE_EXCEEDED`. Python cannot be interrupted safely, so killing
  is the only hard stop.
- **Remote-call timeout.** `remote_call_timeout` (default 300 s) resolves the
  pending nested call with `DEADLINE_EXCEEDED`; the worker remains usable.
- **Crash.** An unexpected exit fails the in-flight task with `INTERNAL`, drops
  outstanding nested-call receivers, and respawns the worker. The task is not
  retried automatically.
- **Client disconnect.** The running worker is killed, its result is dropped,
  and the slot becomes available after the worker exits. The close callback is
  unregistered on normal terminal outcomes.
- **Nested calls** are submitted through `ChildTaskSubmitter` as ordinary
  `pyfunction` children. Locally they use the default scheduler; otherwise the
  gateway forward path applies. The handler never uses a private nested path.

## Worker protocol and packaging

Each slot owns one Python process. Dedicated pipe file descriptors carry
length-prefixed protobuf `WorkerFrame` messages; stdout and stderr remain
ordinary process output and are forwarded to the node logs. The worker is a
Bazel `py_binary` (`//python:worker`) built from the in-tree sources, the
vendored cloudpickle 3.1.1 package, and the protobuf runtime supplied by the
Bazel protobuf module. The nodeagent binary carries the worker and those
runfiles as `data` dependencies, so a deployed worker needs no host `pip`
install.

The worker boundary is language-agnostic. A future embedded C++ worker linked
against libpython can decode the same frames with the C++ protobuf library; it
would no longer need the Python protobuf runtime, and the handler and task
contract would not change.

## Operator deployment

### Runfiles worker

Build the nodeagent and the worker:

```bash
bazel build //src/nodeagent/exe:nodeagent
bazel build //python:worker
```

Point the handler at the worker launcher and keep its runfiles tree next to it:

```yaml
task_handlers:
  - name: "pyfunction"
    typed_config:
      "@type": "type.googleapis.com/strij.extensions.task_handlers.pyfunction.PyFunctionTaskHandlerConfig"
      capacity:
        concurrency: 4
      worker_executable: "/opt/strij/worker"
      startup_timeout:
        seconds: 30
      task_timeout:
        seconds: 0
      remote_call_timeout:
        seconds: 300
      max_frame_bytes: 8388608
```

`worker_executable` is spawned directly (it may be the Bazel launcher, which
finds its own runfiles). Use the runfiles copy of the worker so its vendored
`*_pb2` modules and protobuf runtime are present.

### Installed worker module

Operators who do not deploy runfiles can install the package and use the module
form. The wheel includes the generated `pyfunction` and `capabilities` protobuf
modules, so no Bazel-generated files need to be copied to `python_paths`:

```yaml
task_handlers:
  - name: "pyfunction"
    typed_config:
      "@type": "type.googleapis.com/strij.extensions.task_handlers.pyfunction.PyFunctionTaskHandlerConfig"
      capacity:
        concurrency: 4
      python_executable: "python3"
      worker_module: "strij._worker"
      python_paths:
        - "/opt/strij/python"
      max_frame_bytes: 8388608
```

Validate either form without starting the service:

```bash
nodeagent --config_file config/examples/nodeagent-pyfunction.yaml --validate_only
```

### Routing and nesting headroom

Prefer `capability_aware` gateway routing for `pyfunction`: `round_robin` can
select a node that does not run the handler. Gateway schedulers only consider
nodes that advertise their required wire protocol, so the node config must also
list the `push` scheduler (the bundled `default` scheduler is the local
authority and advertises no protocol). Without it, submissions fail with
`CAPACITY_REFUSED: no eligible node for task submission`. A nested call needs a
free slot on some node. With every local slot blocked in a parent, the child is forwarded;
if no node can admit it, the pending call fails fast with `CAPACITY_REFUSED`
rather than deadlocking. Size `concurrency` for the expected nesting width, or
run parents and children on different nodes.

Because a hung task can occupy a slot indefinitely, set `task_timeout` in
production. At-most-once semantics mean a crashed task is not retried; side
effects must be idempotent across retries initiated by the caller.
