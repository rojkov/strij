"""Invocation envelopes and execution for both pyfunction modes.

The module is split into two halves:

* the versioned cloudpickle envelope used by the Python SDK (pickle mode), and
* the reference/JSON execution path used by JSON producers such as OpenWorkflow.

``InvocationError`` carries the ``TaskStatus`` the worker should report, so the
handler never needs to understand Python failures.
"""

from __future__ import annotations

import json
import sys
import traceback
from dataclasses import dataclass
from typing import Any, Dict, Iterable, Mapping, Optional, Tuple

# Mirrors strij.task.TaskStatus. Kept as plain integers so this module has no
# protobuf import; the generated enum accepts the same numbers.
STATUS_OK = 0
STATUS_MALFORMED_REQUEST = 1
STATUS_NOT_FOUND = 2
STATUS_UNIMPLEMENTED = 3
STATUS_DEADLINE_EXCEEDED = 4
STATUS_CAPACITY_REFUSED = 5
STATUS_INTERNAL = 6
STATUS_UNAVAILABLE = 7

INVOCATION_VERSION = 1
_VERSION_KEY = "__strij_invocation__"

# A cloudpickle body is only portable between matching Python minor versions:
# unpickling a code object produced by a different interpreter can crash the
# process before an exception can be raised. The client therefore prefixes the
# pickle with a small, plain-text header so the worker can reject an
# incompatible interpreter with a clear error instead of dying silently.
_WIRE_MAGIC = b"STRIJ-PY/1:"


class InvocationError(Exception):
    """A classified invocation failure.

    ``status`` is one of the ``STATUS_*`` integers above; ``message`` is the
    text the handler reports as the task's error body.
    """

    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


@dataclass
class Invocation:
    """The deserialized versioned pickle envelope."""

    function: Any
    args: Tuple[Any, ...]
    kwargs: Dict[str, Any]
    version: int = INVOCATION_VERSION


def _cloudpickle():
    # Imported lazily so importing ``strij`` does not require the dependency.
    import cloudpickle  # noqa: PLC0415

    return cloudpickle


def _encode_invocation(payload: bytes) -> bytes:
    version = "%d.%d.%d" % sys.version_info[:3]
    return _WIRE_MAGIC + version.encode("ascii") + b"\n" + payload


def _decode_invocation(body: bytes) -> bytes:
    """Validates the Python-version header before any pickle is unpacked."""
    if not body.startswith(_WIRE_MAGIC):
        # Bodies written by older clients have no header. They are still
        # accepted, but the worker cannot pre-check their interpreter version.
        return body

    header_end = body.find(b"\n", len(_WIRE_MAGIC))
    if header_end < 0:
        raise InvocationError(
            STATUS_MALFORMED_REQUEST,
            "the task body has a truncated Python version header",
        )

    raw_version = body[len(_WIRE_MAGIC) : header_end]
    try:
        parts = tuple(int(part) for part in raw_version.decode("ascii").split("."))
    except (UnicodeDecodeError, ValueError) as exc:
        raise InvocationError(
            STATUS_MALFORMED_REQUEST,
            "the task body has an invalid Python version header",
        ) from exc

    if len(parts) < 2:
        raise InvocationError(
            STATUS_MALFORMED_REQUEST,
            "the task body has an incomplete Python version header",
        )

    client_version = parts[:2]
    worker_version = sys.version_info[:2]
    if client_version != worker_version:
        raise InvocationError(
            STATUS_MALFORMED_REQUEST,
            "pickle-mode invocations require matching Python versions: the "
            "client used Python %s but the worker runs Python %d.%d; submit "
            "from a client whose Python minor version matches the worker"
            % (".".join(str(part) for part in parts), worker_version[0], worker_version[1]),
        )

    return body[header_end + 1 :]


def serialize_invocation(function: Any, args: Iterable[Any], kwargs: Mapping[str, Any]) -> bytes:
    """Serializes a pickle-mode invocation with its explicit version."""
    payload = {
        _VERSION_KEY: INVOCATION_VERSION,
        "function": function,
        "args": tuple(args),
        "kwargs": dict(kwargs),
    }
    try:
        return _encode_invocation(_cloudpickle().dumps(payload))
    except Exception as exc:  # noqa: BLE001 - surfaced as a classified error
        raise InvocationError(
            STATUS_MALFORMED_REQUEST, "the invocation cannot be serialized: %s" % exc
        ) from exc


def deserialize_invocation(body: bytes) -> Invocation:
    """Decodes and validates a pickle-mode envelope."""
    body = _decode_invocation(body)
    try:
        payload = _cloudpickle().loads(body)
    except Exception as exc:  # noqa: BLE001 - unpicklable body is malformed
        raise InvocationError(
            STATUS_MALFORMED_REQUEST, "the task body is not a valid Invocation: %s" % exc
        ) from exc

    if not isinstance(payload, dict) or _VERSION_KEY not in payload:
        raise InvocationError(
            STATUS_MALFORMED_REQUEST,
            "the task body is not a Strij Invocation envelope",
        )

    version = payload[_VERSION_KEY]
    if version != INVOCATION_VERSION:
        raise InvocationError(
            STATUS_MALFORMED_REQUEST,
            "unsupported Invocation version %r (this worker supports %d)"
            % (version, INVOCATION_VERSION),
        )

    try:
        function = payload["function"]
        args = tuple(payload.get("args", ()))
        kwargs = dict(payload.get("kwargs", {}))
    except (KeyError, TypeError, ValueError) as exc:
        raise InvocationError(
            STATUS_MALFORMED_REQUEST, "the Invocation envelope is malformed: %s" % exc
        ) from exc

    return Invocation(function=function, args=args, kwargs=kwargs, version=version)


def _contains_ref(value: Any) -> bool:
    from ._runtime import RemoteRef  # noqa: PLC0415 - avoids an import cycle

    if isinstance(value, RemoteRef):
        return True
    if isinstance(value, (list, tuple, set, frozenset)):
        return any(_contains_ref(item) for item in value)
    if isinstance(value, dict):
        return any(_contains_ref(k) or _contains_ref(v) for k, v in value.items())
    return False


def _format_exception(exc: BaseException) -> str:
    return "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))


def execute_pickle(body: bytes) -> Tuple[bytes, int, str]:
    """Runs a pickle-mode invocation and returns (body, status, reason)."""
    try:
        invocation = deserialize_invocation(body)
    except InvocationError as exc:
        return b"", exc.status, exc.message

    try:
        result = invocation.function(*invocation.args, **invocation.kwargs)
    except BaseException as exc:  # noqa: BLE001 - user code may raise anything
        return b"", STATUS_INTERNAL, _format_exception(exc)

    if _contains_ref(result):
        return (
            b"",
            STATUS_INTERNAL,
            "a task cannot return a RemoteRef; call .get() before returning",
        )

    try:
        return _cloudpickle().dumps(result), STATUS_OK, ""
    except Exception as exc:  # noqa: BLE001 - unpicklable result is internal
        return b"", STATUS_INTERNAL, "the result cannot be serialized: %s" % exc


def _resolve_entry(namespace: Mapping[str, Any], entry: str) -> Any:
    if not entry:
        raise InvocationError(STATUS_MALFORMED_REQUEST, "embedded function entry is empty")

    target: Any = namespace
    for part in entry.split("."):
        if not part:
            raise InvocationError(
                STATUS_MALFORMED_REQUEST, "embedded function entry %r is malformed" % entry
            )
        try:
            target = target[part] if isinstance(target, Mapping) else getattr(target, part)
        except (KeyError, AttributeError) as exc:
            raise InvocationError(
                STATUS_NOT_FOUND, "embedded entry %r cannot be resolved" % entry
            ) from exc
    return target


def _resolve_reference(function: str, body: Mapping[str, Any]) -> Any:
    if function.startswith("embedded:"):
        entry = function[len("embedded:") :]
        code = body.get("python_code")
        if not isinstance(code, str):
            raise InvocationError(
                STATUS_MALFORMED_REQUEST,
                "embedded function requires a string 'python_code' field",
            )
        namespace: Dict[str, Any] = {"__name__": "__strij_embedded__"}
        try:
            exec(compile(code, "<strij-embedded>", "exec"), namespace)  # noqa: S102
        except Exception as exc:  # noqa: BLE001 - source errors are malformed
            raise InvocationError(
                STATUS_MALFORMED_REQUEST,
                "embedded python_code failed to execute:\n%s" % _format_exception(exc),
            ) from exc
        return _resolve_entry(namespace, entry)

    if function.startswith("module:"):
        rest = function[len("module:") :]
        module_name, separator, qualname = rest.partition(":")
        if not module_name or not separator or not qualname:
            raise InvocationError(
                STATUS_MALFORMED_REQUEST,
                "module reference must be 'module:<module>:<qualname>'",
            )
        try:
            module = __import__(module_name, fromlist=["*"])
        except Exception as exc:  # noqa: BLE001 - import failures are not found
            raise InvocationError(
                STATUS_NOT_FOUND,
                "module %r cannot be imported: %s" % (module_name, exc),
            ) from exc
        target: Any = module
        for part in qualname.split("."):
            try:
                target = getattr(target, part)
            except AttributeError as exc:
                raise InvocationError(
                    STATUS_NOT_FOUND,
                    "module reference %r cannot be resolved" % function,
                ) from exc
        return target

    raise InvocationError(
        STATUS_MALFORMED_REQUEST,
        "unsupported function reference %r; expected 'embedded:<entry>' or "
        "'module:<module>:<qualname>'" % function,
    )


def _bind_json_arguments(body: Mapping[str, Any]) -> Tuple[list, dict]:
    data = {key: value for key, value in body.items() if key != "python_code"}
    has_args = "args" in data
    has_kwargs = "kwargs" in data
    has_input = "input" in data

    if has_input and (has_args or has_kwargs):
        raise InvocationError(
            STATUS_MALFORMED_REQUEST,
            "the body must not mix 'input' with 'args' or 'kwargs'",
        )

    if has_args or has_kwargs:
        args = data.get("args", [])
        kwargs = data.get("kwargs", {})
        if not isinstance(args, list):
            raise InvocationError(STATUS_MALFORMED_REQUEST, "'args' must be a list")
        if not isinstance(kwargs, dict):
            raise InvocationError(STATUS_MALFORMED_REQUEST, "'kwargs' must be an object")
        return args, kwargs

    if has_input:
        return [data["input"]], {}

    return [data], {}


def execute_json(function: str, body: bytes) -> Tuple[bytes, int, str]:
    """Runs a reference/JSON invocation and returns (body, status, reason)."""
    try:
        parsed = json.loads(body)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return b"", STATUS_MALFORMED_REQUEST, "the task body is not valid JSON: %s" % exc

    if not isinstance(parsed, dict):
        return b"", STATUS_MALFORMED_REQUEST, "the task body must be a JSON object"

    try:
        entry = _resolve_reference(function, parsed)
        if not callable(entry):
            raise InvocationError(
                STATUS_NOT_FOUND, "function reference %r does not resolve to a callable" % function
            )
        args, kwargs = _bind_json_arguments(parsed)
        result = entry(*args, **kwargs)
    except InvocationError as exc:
        return b"", exc.status, exc.message
    except Exception as exc:  # noqa: BLE001 - user code may raise anything
        return b"", STATUS_INTERNAL, _format_exception(exc)

    try:
        return json.dumps(result).encode("utf-8"), STATUS_OK, ""
    except (TypeError, ValueError) as exc:
        return b"", STATUS_INTERNAL, "the result is not JSON serializable: %s" % exc


def execute(mode: int, function: str, body: bytes) -> Tuple[bytes, int, str]:
    """Dispatches one run frame by mode."""
    if mode == 1:  # INVOCATION_MODE_JSON
        return execute_json(function, body)
    if mode == 2:  # INVOCATION_MODE_PICKLE
        return execute_pickle(body)
    return b"", STATUS_MALFORMED_REQUEST, "unknown invocation mode %r" % mode
