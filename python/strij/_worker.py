"""Worker bootstrap for the ``pyfunction`` handler.

The handler spawns this module (or the Bazel ``py_binary`` wrapping it) once per
slot. Dedicated pipe file descriptors carry length-prefixed protobuf frames;
stdout/stderr remain ordinary process output and go to the node logs.

Import order matters: cloudpickle and the generated protobuf modules are
imported before the readiness frame is sent. A missing dependency therefore
leaves the slot unavailable instead of accepting work it cannot run.
"""

from __future__ import annotations

import os
import queue
import struct
import sys
import threading
import traceback
from typing import Any, Optional

PROTOCOL_VERSION = 1
_ENV_FD_IN = "STRIJ_PYFUNCTION_FD_IN"
_ENV_FD_OUT = "STRIJ_PYFUNCTION_FD_OUT"
_ENV_MAX_FRAME = "STRIJ_PYFUNCTION_MAX_FRAME_BYTES"
_DEFAULT_MAX_FRAME = 8 * 1024 * 1024


def _read_exact(fd: int, size: int) -> Optional[bytes]:
    chunks = bytearray()
    while len(chunks) < size:
        data = os.read(fd, size - len(chunks))
        if not data:
            return None
        chunks.extend(data)
    return bytes(chunks)


def read_frame(fd: int, pb: Any, max_frame_bytes: int) -> Optional[Any]:
    """Reads one frame; returns None on clean EOF."""
    header = _read_exact(fd, 4)
    if header is None:
        return None
    (length,) = struct.unpack(">I", header)
    if length > max_frame_bytes:
        raise ValueError(
            "frame length %d exceeds the configured maximum %d" % (length, max_frame_bytes)
        )
    payload = _read_exact(fd, length)
    if payload is None:
        return None
    frame = pb.WorkerFrame()
    frame.ParseFromString(payload)
    return frame


class FrameWriter:
    """Serializes writes so concurrent user threads cannot interleave frames."""

    def __init__(self, fd: int, max_frame_bytes: int) -> None:
        self._fd = fd
        self._max_frame_bytes = max_frame_bytes
        self._lock = threading.Lock()

    def send(self, frame: Any) -> None:
        payload = frame.SerializeToString()
        if len(payload) > self._max_frame_bytes:
            raise ValueError(
                "frame length %d exceeds the configured maximum %d"
                % (len(payload), self._max_frame_bytes)
            )
        data = struct.pack(">I", len(payload)) + payload
        with self._lock:
            view = memoryview(data)
            while view:
                written = os.write(self._fd, view)
                view = view[written:]


class _Shutdown:
    pass


def _reader_loop(
    fd: int,
    pb: Any,
    runtime: Any,
    runs: "queue.Queue[Any]",
    max_frame_bytes: int,
) -> None:
    try:
        while True:
            frame = read_frame(fd, pb, max_frame_bytes)
            if frame is None:
                runs.put(_Shutdown())
                return
            kind = frame.WhichOneof("frame")
            if kind == "run":
                runs.put(frame.run)
            elif kind == "remote_result":
                runtime.on_remote_result(frame)
            elif kind == "shutdown":
                runs.put(_Shutdown())
                return
    except Exception:  # noqa: BLE001 - report and let the handler see EOF
        traceback.print_exc()
        runs.put(_Shutdown())


def main() -> int:
    # User print() belongs on the log stream; protocol bytes use dedicated fds.
    sys.stdout = sys.stderr

    in_fd = int(os.environ.get(_ENV_FD_IN, "3"))
    out_fd = int(os.environ.get(_ENV_FD_OUT, "4"))
    max_frame_bytes = int(os.environ.get(_ENV_MAX_FRAME, str(_DEFAULT_MAX_FRAME)))

    try:
        import cloudpickle  # noqa: F401, PLC0415

        from strij._proto import pyfunction_pb2 as pb

        import strij  # noqa: PLC0415

        from strij import _runtime  # noqa: PLC0415

        # Importing the generated modules validates the protobuf runtime
        # version before readiness is announced.
        _ = pb.WorkerFrame()
    except Exception:  # noqa: BLE001 - any import failure means "not ready"
        traceback.print_exc()
        return 2

    handshake = read_frame(in_fd, pb, max_frame_bytes)
    if handshake is None or handshake.WhichOneof("frame") != "hello":
        print(
            "strij worker: expected a Hello handshake before readiness",
            file=sys.stderr,
        )
        return 2
    if handshake.hello.version != PROTOCOL_VERSION:
        print(
            "strij worker: unsupported handler protocol version %d (expected %d)"
            % (handshake.hello.version, PROTOCOL_VERSION),
            file=sys.stderr,
        )
        return 2

    writer = FrameWriter(out_fd, max_frame_bytes)

    def send_call(call_id: str, invocation: bytes, requirements: Any) -> None:
        frame = pb.WorkerFrame()
        call = frame.remote_call
        call.call_id = call_id
        call.invocation = invocation
        for pool, amount in dict(requirements).items():
            call.requirements[pool] = int(amount)
        writer.send(frame)

    runtime = _runtime.WorkerRuntime(send_call)
    _runtime._set_runtime(runtime)  # noqa: SLF001 - bootstrap installs the runtime
    _ = strij

    ready = pb.WorkerFrame()
    ready.ready.pid = os.getpid()
    ready.ready.version = PROTOCOL_VERSION
    writer.send(ready)

    runs: "queue.Queue[Any]" = queue.Queue()
    reader = threading.Thread(
        target=_reader_loop,
        args=(in_fd, pb, runtime, runs, max_frame_bytes),
        name="strij-protocol-reader",
        daemon=True,
    )
    reader.start()

    while True:
        run = runs.get()
        if isinstance(run, _Shutdown):
            return 0

        from strij import _invocation  # noqa: PLC0415

        body, status, reason = _invocation.execute(run.mode, run.function, run.body)
        frame = pb.WorkerFrame()
        result = frame.run_result
        result.task_id = run.task_id
        result.body = body
        result.is_final = True
        result.status = status
        result.reason = reason
        try:
            writer.send(frame)
        except Exception:  # noqa: BLE001 - the handler is gone
            traceback.print_exc()
            return 0


if __name__ == "__main__":
    sys.exit(main())
