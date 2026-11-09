"""Runtime selection, remote calls, and reference lifecycle.

A process-global runtime is installed either by the worker bootstrap (pipe
transport) or by :func:`init` (HTTP client). ``@remote`` is identical in both
contexts; only the runtime that performs the submission changes.
"""

from __future__ import annotations

import functools
import threading
import urllib.error
import urllib.request
import uuid
from enum import IntEnum
from typing import Any, Callable, Dict, Mapping, Optional

from . import _invocation


class TaskStatus(IntEnum):
    """Mirror of ``strij.task.TaskStatus`` for SDK consumers."""

    OK = 0
    MALFORMED_REQUEST = 1
    NOT_FOUND = 2
    UNIMPLEMENTED = 3
    DEADLINE_EXCEEDED = 4
    CAPACITY_REFUSED = 5
    INTERNAL = 6
    UNAVAILABLE = 7


_HTTP_TO_STATUS = {
    400: TaskStatus.MALFORMED_REQUEST,
    404: TaskStatus.NOT_FOUND,
    429: TaskStatus.CAPACITY_REFUSED,
    500: TaskStatus.INTERNAL,
    501: TaskStatus.UNIMPLEMENTED,
    503: TaskStatus.UNAVAILABLE,
    504: TaskStatus.DEADLINE_EXCEEDED,
}


def status_from_http(code: int) -> TaskStatus:
    return _HTTP_TO_STATUS.get(int(code), TaskStatus.INTERNAL)


class RemoteError(Exception):
    """A remote task failed with a classified status."""

    def __init__(self, status: TaskStatus, message: str) -> None:
        super().__init__(message)
        self.status = TaskStatus(status)
        self.message = message

    def __str__(self) -> str:
        return "%s: %s" % (self.status.name, self.message)


class RemoteTimeoutError(TimeoutError):
    """``.get(timeout=...)`` expired; the remote task keeps running."""


class RemoteRef:
    """A handle to a submitted computation.

    ``get()`` is idempotent: a resolved ref returns the same value and a failed
    ref raises the same error. A timeout raises :class:`RemoteTimeoutError`
    without cancelling the task.
    """

    def __init__(
        self, runtime: "_Runtime", call_id: str, default_timeout: Optional[float] = None
    ) -> None:
        self._runtime = runtime
        self._call_id = call_id
        self._default_timeout = default_timeout
        self._event = threading.Event()
        self._value: Any = None
        self._error: Optional[BaseException] = None
        self._done = False
        self._lock = threading.Lock()

    def _resolve(self, value: Any) -> None:
        with self._lock:
            if self._done:
                return
            self._value = value
            self._done = True
        self._event.set()

    def _reject(self, error: BaseException) -> None:
        with self._lock:
            if self._done:
                return
            self._error = error
            self._done = True
        self._event.set()

    def get(self, timeout: Optional[float] = None) -> Any:
        wait = self._default_timeout if timeout is None else timeout
        if not self._event.wait(wait):
            raise RemoteTimeoutError(
                "remote call %s did not complete within %s seconds" % (self._call_id, wait)
            )
        # Once set, _error/_value are immutable.
        if self._error is not None:
            raise self._error
        if isinstance(self._value, RemoteRef):
            raise RemoteError(
                TaskStatus.INTERNAL,
                "a task returned a RemoteRef; call .get() before returning",
            )
        return self._value


def _contains_ref(value: Any) -> bool:
    if isinstance(value, RemoteRef):
        return True
    if isinstance(value, (list, tuple, set, frozenset)):
        return any(_contains_ref(item) for item in value)
    if isinstance(value, dict):
        return any(_contains_ref(k) or _contains_ref(v) for k, v in value.items())
    return False


class _Runtime:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._refs: Dict[str, RemoteRef] = {}
        self._partial: Dict[str, bytearray] = {}

    # -- eager reference resolution -------------------------------------
    def resolve(self, value: Any) -> Any:
        if isinstance(value, RemoteRef):
            if value._runtime is not self:
                raise RuntimeError(
                    "a RemoteRef created by a different Strij runtime cannot be "
                    "resolved here; resolve it in its owning process before submission"
                )
            return value.get()
        if isinstance(value, list):
            return [self.resolve(item) for item in value]
        if isinstance(value, tuple):
            return tuple(self.resolve(item) for item in value)
        if isinstance(value, set):
            return {self.resolve(item) for item in value}
        if isinstance(value, frozenset):
            return frozenset(self.resolve(item) for item in value)
        if isinstance(value, dict):
            return {self.resolve(k): self.resolve(v) for k, v in value.items()}
        return value

    # -- submission ------------------------------------------------------
    def submit(
        self,
        function: Callable[..., Any],
        args: tuple,
        kwargs: Mapping[str, Any],
        requirements: Optional[Mapping[str, int]],
        timeout: Optional[float],
    ) -> RemoteRef:
        resolved_args = tuple(self.resolve(value) for value in args)
        resolved_kwargs = {
            key: self.resolve(value) for key, value in kwargs.items()
        }
        if _contains_ref(resolved_kwargs) or _contains_ref(resolved_args):
            raise RuntimeError("a RemoteRef remained after eager resolution")

        payload = _invocation.serialize_invocation(
            function, resolved_args, resolved_kwargs
        )
        return self._submit_serialized(payload, requirements or {}, timeout)

    def _submit_serialized(
        self,
        payload: bytes,
        requirements: Mapping[str, int],
        timeout: Optional[float],
    ) -> RemoteRef:
        raise NotImplementedError

    def _register(self, ref: RemoteRef) -> None:
        with self._lock:
            self._refs[ref._call_id] = ref

    def _pop(self, call_id: str) -> Optional[RemoteRef]:
        with self._lock:
            return self._refs.pop(call_id, None)


class WorkerRuntime(_Runtime):
    """Pipe-backed runtime installed by the worker bootstrap.

    ``send_call`` writes a ``RemoteCall`` frame; it is supplied by the
    bootstrap because it owns the protobuf module and the serialized writer.
    The bootstrap's reader thread calls :meth:`on_remote_result`.
    """

    def __init__(self, send_call: Callable[[str, bytes, Mapping[str, int]], None]) -> None:
        super().__init__()
        self._send_call = send_call

    def _submit_serialized(
        self,
        payload: bytes,
        requirements: Mapping[str, int],
        timeout: Optional[float],
    ) -> RemoteRef:
        call_id = uuid.uuid4().hex
        ref = RemoteRef(self, call_id, default_timeout=timeout)
        self._register(ref)
        self._send_call(call_id, payload, requirements)
        return ref

    def on_remote_result(self, frame: Any) -> None:
        """Resolves a pending ref from a ``RemoteResult`` frame."""
        result = frame.remote_result
        call_id = result.call_id
        with self._lock:
            partial = self._partial.setdefault(call_id, bytearray())
            partial.extend(result.body)
            body = bytes(partial)
            is_final = result.is_final
            if is_final:
                self._partial.pop(call_id, None)
            ref = self._refs.get(call_id)

        if result.status != TaskStatus.OK:
            with self._lock:
                ref = self._refs.pop(call_id, None)
                self._partial.pop(call_id, None)
            if ref is not None:
                ref._reject(RemoteError(TaskStatus(result.status), result.reason))
            return

        if is_final:
            ref = self._pop(call_id)
            if ref is None:
                return
            try:
                value = _invocation._cloudpickle().loads(body)  # noqa: SLF001
            except Exception as exc:  # noqa: BLE001 - corrupt remote payload
                ref._reject(
                    RemoteError(
                        TaskStatus.INTERNAL,
                        "the remote result could not be decoded: %s" % exc,
                    )
                )
                return
            ref._resolve(value)


class HttpClientRuntime(_Runtime):
    """HTTP client runtime installed by ``strij.init(gateway_url)``."""

    def __init__(self, gateway_url: str, request_timeout: Optional[float] = None) -> None:
        super().__init__()
        self._gateway_url = gateway_url.rstrip("/")
        self._request_timeout = request_timeout

    def _submit_serialized(
        self,
        payload: bytes,
        requirements: Mapping[str, int],
        timeout: Optional[float],
    ) -> RemoteRef:
        call_id = uuid.uuid4().hex
        ref = RemoteRef(self, call_id, default_timeout=timeout)
        self._register(ref)
        thread = threading.Thread(
            target=self._run, args=(ref, payload, requirements), daemon=True
        )
        thread.start()
        return ref

    def _run(self, ref: RemoteRef, payload: bytes, requirements: Mapping[str, int]) -> None:
        url = self._gateway_url + "/tasks/pyfunction"
        headers = {"Content-Type": "application/octet-stream"}
        for pool, amount in requirements.items():
            headers["X-Strij-Resources-%s" % pool] = str(amount)

        request = urllib.request.Request(url, data=payload, headers=headers, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=self._request_timeout) as response:
                body = response.read()
        except urllib.error.HTTPError as exc:
            reason = _read_error_body(exc)
            ref._reject(RemoteError(status_from_http(exc.code), reason))
            return
        except urllib.error.URLError as exc:
            ref._reject(
                RemoteError(TaskStatus.UNAVAILABLE, "the gateway is unreachable: %s" % exc.reason)
            )
            return
        except Exception as exc:  # noqa: BLE001 - transport failures are unavailable
            ref._reject(RemoteError(TaskStatus.UNAVAILABLE, str(exc)))
            return

        try:
            value = _invocation._cloudpickle().loads(body)  # noqa: SLF001
        except Exception as exc:  # noqa: BLE001 - corrupt response body
            ref._reject(
                RemoteError(
                    TaskStatus.INTERNAL,
                    "the response body could not be decoded as a Python result: %s" % exc,
                )
            )
            return
        ref._resolve(value)


def _read_error_body(exc: urllib.error.HTTPError) -> str:
    try:
        data = exc.read()
    except Exception:  # noqa: BLE001
        data = b""
    if isinstance(data, bytes):
        text = data.decode("utf-8", errors="replace")
    else:
        text = str(data)
    return text or str(exc.reason)


_runtime: Optional[_Runtime] = None


def _set_runtime(runtime: Optional[_Runtime]) -> None:
    global _runtime  # noqa: PLW0603
    _runtime = runtime


def _get_runtime() -> _Runtime:
    if _runtime is None:
        raise RuntimeError(
            "no Strij runtime configured: call strij.init('http://gateway:port') in a "
            "client process, or run inside a pyfunction worker"
        )
    return _runtime


def init(gateway_url: str, request_timeout: Optional[float] = None) -> _Runtime:
    """Installs (and returns) the HTTP client runtime."""
    runtime = HttpClientRuntime(gateway_url, request_timeout=request_timeout)
    _set_runtime(runtime)
    return runtime


def remote(
    function: Optional[Callable[..., Any]] = None,
    *,
    requirements: Optional[Mapping[str, int]] = None,
    timeout: Optional[float] = None,
) -> Any:
    """Decorates a callable; ``fn.remote(...)`` submits it and returns a ref."""

    def decorate(func: Callable[..., Any]) -> Callable[..., Any]:
        @functools.wraps(func)
        def wrapper(*args: Any, **kwargs: Any) -> Any:
            return func(*args, **kwargs)

        def submit(*args: Any, **kwargs: Any) -> RemoteRef:
            return _get_runtime().submit(function=func, args=args, kwargs=kwargs,
                                         requirements=requirements, timeout=timeout)

        wrapper.remote = submit  # type: ignore[attr-defined]
        return wrapper

    if function is not None:
        return decorate(function)
    return decorate
