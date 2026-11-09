import json
import os
import struct
import subprocess
import sys
import tempfile
import threading
import unittest

from strij._proto import pyfunction_pb2 as pb


def _runfile(*parts: str) -> str:
    srcdir = os.environ.get("TEST_SRCDIR", "")
    workspace = os.environ.get("TEST_WORKSPACE", "_main")
    return os.path.join(srcdir, workspace, *parts)


def _worker_path() -> str:
    return _runfile("python", "worker")


def _read_exact(fd: int, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        data = os.read(fd, size - len(chunks))
        if not data:
            raise EOFError("worker pipe closed")
        chunks.extend(data)
    return bytes(chunks)


def _read_frame(fd: int) -> pb.WorkerFrame:
    (length,) = struct.unpack(">I", _read_exact(fd, 4))
    frame = pb.WorkerFrame()
    frame.ParseFromString(_read_exact(fd, length))
    return frame


def _write_frame(fd: int, frame: pb.WorkerFrame) -> None:
    payload = frame.SerializeToString()
    os.write(fd, struct.pack(">I", len(payload)) + payload)


class WorkerBootstrapTest(unittest.TestCase):
    def setUp(self) -> None:
        self.to_worker_r, self.to_worker_w = os.pipe()
        self.from_worker_r, self.from_worker_w = os.pipe()
        env = dict(os.environ)
        env["STRIJ_PYFUNCTION_FD_IN"] = str(self.to_worker_r)
        env["STRIJ_PYFUNCTION_FD_OUT"] = str(self.from_worker_w)
        self.process = subprocess.Popen(
            [_worker_path()],
            env=env,
            pass_fds=(self.to_worker_r, self.from_worker_w),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        os.close(self.to_worker_r)
        os.close(self.from_worker_w)

    def tearDown(self) -> None:
        os.close(self.to_worker_w)
        os.close(self.from_worker_r)
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=10)

    def _send_hello(self, version: int = 1) -> None:
        hello = pb.WorkerFrame()
        hello.hello.version = version
        _write_frame(self.to_worker_w, hello)

    def test_ready_run_and_shutdown(self) -> None:
        self._send_hello()
        ready = _read_frame(self.from_worker_r)
        self.assertEqual(ready.WhichOneof("frame"), "ready")
        self.assertEqual(ready.ready.version, 1)
        self.assertEqual(ready.ready.pid, self.process.pid)

        run = pb.WorkerFrame()
        run.run.task_id = "bootstrap-1"
        run.run.mode = pb.INVOCATION_MODE_JSON
        run.run.function = "embedded:main"
        run.run.body = (
            b'{"python_code": "def main(value):\\n    print(\'hello-from-worker\')\\n'
            b'    return value + 1", "input": 4}'
        )
        _write_frame(self.to_worker_w, run)

        result = _read_frame(self.from_worker_r)
        self.assertEqual(result.WhichOneof("frame"), "run_result")
        self.assertEqual(result.run_result.task_id, "bootstrap-1")
        self.assertEqual(result.run_result.status, 0)
        self.assertEqual(result.run_result.body, b"5")
        self.assertTrue(result.run_result.is_final)

        shutdown = pb.WorkerFrame()
        shutdown.shutdown.SetInParent()
        _write_frame(self.to_worker_w, shutdown)

        self.assertEqual(self.process.wait(timeout=10), 0)
        stderr = self.process.stderr.read().decode("utf-8", errors="replace")
        # User print() goes to the log stream, never the protocol pipe.
        self.assertIn("hello-from-worker", stderr)

    def test_pickle_mode_round_trip(self) -> None:
        self._send_hello()
        ready = _read_frame(self.from_worker_r)
        self.assertEqual(ready.WhichOneof("frame"), "ready")

        import cloudpickle

        def add(left, right):  # noqa: ANN001, ANN202 - local test callable
            return left + right

        body = cloudpickle.dumps(
            {
                "__strij_invocation__": 1,
                "function": add,
                "args": (2, 3),
                "kwargs": {},
            }
        )
        run = pb.WorkerFrame()
        run.run.task_id = "bootstrap-2"
        run.run.mode = pb.INVOCATION_MODE_PICKLE
        run.run.body = body
        _write_frame(self.to_worker_w, run)

        result = _read_frame(self.from_worker_r)
        self.assertEqual(result.run_result.status, 0)
        self.assertEqual(cloudpickle.loads(result.run_result.body), 5)

    def test_remote_call_frame_carries_requirements(self) -> None:
        self._send_hello()
        ready = _read_frame(self.from_worker_r)
        self.assertEqual(ready.WhichOneof("frame"), "ready")

        import cloudpickle

        python_code = (
            "import strij\n"
            "@strij.remote(requirements={'cpu': 1})\n"
            "def child():\n"
            "    return 1\n"
            "def main(_payload):\n"
            "    return child.remote().get()\n"
        )
        run = pb.WorkerFrame()
        run.run.task_id = "bootstrap-3"
        run.run.mode = pb.INVOCATION_MODE_JSON
        run.run.function = "embedded:main"
        run.run.body = json.dumps({"python_code": python_code}).encode("utf-8")
        _write_frame(self.to_worker_w, run)

        call = _read_frame(self.from_worker_r)
        self.assertEqual(call.WhichOneof("frame"), "remote_call")
        self.assertEqual(dict(call.remote_call.requirements), {"cpu": 1})

        reply = pb.WorkerFrame()
        reply.remote_result.call_id = call.remote_call.call_id
        reply.remote_result.body = cloudpickle.dumps(9)
        reply.remote_result.is_final = True
        reply.remote_result.status = 0
        _write_frame(self.to_worker_w, reply)

        result = _read_frame(self.from_worker_r)
        self.assertEqual(result.WhichOneof("frame"), "run_result")
        self.assertEqual(result.run_result.task_id, "bootstrap-3")
        self.assertEqual(result.run_result.status, 0)
        self.assertEqual(json.loads(result.run_result.body), 9)

        shutdown = pb.WorkerFrame()
        shutdown.shutdown.SetInParent()
        _write_frame(self.to_worker_w, shutdown)

    def test_rejects_mismatched_handler_version(self) -> None:
        self._send_hello(999)
        returncode = self.process.wait(timeout=10)
        payload = os.read(self.from_worker_r, 1)
        stderr = self.process.stderr.read().decode("utf-8", errors="replace")

        self.assertNotEqual(returncode, 0)
        self.assertEqual(payload, b"")
        self.assertIn("unsupported handler protocol version", stderr)

    def test_concurrent_frame_writes_do_not_interleave(self) -> None:
        from strij._worker import FrameWriter

        read_fd, write_fd = os.pipe()
        writer = FrameWriter(write_fd, 1 << 20)
        barrier = threading.Barrier(5)
        errors: list[BaseException] = []

        def send(index: int) -> None:
            try:
                frame = pb.WorkerFrame()
                frame.run.task_id = "concurrent-%d" % index
                frame.run.body = bytes([65 + index]) * 8192
                barrier.wait()
                writer.send(frame)
            except BaseException as exc:  # noqa: BLE001 - surfaced below
                errors.append(exc)

        threads = [threading.Thread(target=send, args=(index,)) for index in range(4)]
        for thread in threads:
            thread.start()
        barrier.wait()
        for thread in threads:
            thread.join()
        os.close(write_fd)

        try:
            frames = [_read_frame(read_fd) for _ in range(4)]
        finally:
            os.close(read_fd)

        self.assertEqual(errors, [])
        bodies = {frame.run.task_id: frame.run.body for frame in frames}
        self.assertEqual(len(bodies), 4)
        for index in range(4):
            self.assertEqual(bodies["concurrent-%d" % index], bytes([65 + index]) * 8192)


class WorkerMissingProtobufTest(unittest.TestCase):
    def test_worker_without_protobuf_does_not_become_ready(self) -> None:
        python_root = _runfile("python")
        third_party = _runfile("third_party", "python")
        if not os.path.isdir(python_root) or not os.path.isdir(third_party):
            self.skipTest("worker runfiles are unavailable")

        with tempfile.TemporaryDirectory() as broken:
            google = os.path.join(broken, "google")
            protobuf = os.path.join(google, "protobuf")
            os.makedirs(protobuf)
            with open(os.path.join(google, "__init__.py"), "w", encoding="utf-8"):
                pass
            with open(os.path.join(protobuf, "__init__.py"), "w", encoding="utf-8") as handle:
                handle.write("raise ImportError('simulated missing protobuf runtime')\n")

            to_worker_r, to_worker_w = os.pipe()
            from_worker_r, from_worker_w = os.pipe()
            env = dict(os.environ)
            env["STRIJ_PYFUNCTION_FD_IN"] = str(to_worker_r)
            env["STRIJ_PYFUNCTION_FD_OUT"] = str(from_worker_w)
            env["PYTHONPATH"] = os.pathsep.join([broken, python_root, third_party])

            process = subprocess.Popen(
                [sys.executable, "-m", "strij._worker"],
                env=env,
                pass_fds=(to_worker_r, from_worker_w),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            os.close(to_worker_r)
            os.close(from_worker_w)

            try:
                try:
                    returncode = process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
                    self.fail("worker did not exit without a protobuf runtime")
                payload = os.read(from_worker_r, 1)
                stderr = process.stderr.read().decode("utf-8", errors="replace")
            finally:
                os.close(to_worker_w)
                os.close(from_worker_r)
                process.stdout.close()
                process.stderr.close()

            self.assertNotEqual(returncode, 0)
            self.assertEqual(payload, b"")
            self.assertIn("simulated missing protobuf runtime", stderr)


if __name__ == "__main__":
    unittest.main()
