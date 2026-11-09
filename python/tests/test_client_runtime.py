import http.server
import threading
import time
import unittest

import cloudpickle

import strij
from strij import _invocation, _runtime


class _Handler(http.server.BaseHTTPRequestHandler):
    server: "_Server"

    def log_message(self, *_args):  # noqa: ANN002 - silence test server
        pass

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        length = int(self.headers.get("Content-Length", "0"))
        payload = self.rfile.read(length)
        self.server.requests.append((self.path, dict(self.headers), payload))

        if self.server.mode == "slow":
            time.sleep(1.0)
        if self.server.mode.startswith("fail:"):
            code = int(self.server.mode.split(":", 1)[1])
            body = b"server says no"
            self.send_response(code)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        invocation = _invocation.deserialize_invocation(payload)
        result = invocation.function(*invocation.args, **invocation.kwargs)
        body = cloudpickle.dumps(result)
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class _Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address):
        super().__init__(address, _Handler)
        self.mode = "ok"
        self.requests = []


class HttpClientRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.server = _Server(("127.0.0.1", 0))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.url = "http://127.0.0.1:%d" % cls.server.server_address[1]

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()

    def setUp(self) -> None:
        self.server.mode = "ok"
        self.server.requests.clear()
        strij.init(self.url)

    def test_submits_pickle_mode_and_returns_result(self) -> None:
        @strij.remote
        def add(left, right):
            return left + right

        ref = add.remote(2, 5)
        self.assertEqual(ref.get(timeout=5), 7)

        self.assertEqual(len(self.server.requests), 1)
        path, _headers, payload = self.server.requests[0]
        self.assertEqual(path, "/tasks/pyfunction")
        invocation = _invocation.deserialize_invocation(payload)
        self.assertEqual(invocation.version, 1)
        self.assertEqual(invocation.args, (2, 5))

    def test_requirements_become_headers(self) -> None:
        @strij.remote(requirements={"cpu": 2})
        def work():
            return "ok"

        self.assertEqual(work.remote().get(timeout=5), "ok")
        _path, headers, _payload = self.server.requests[0]
        lowered = {key.lower(): value for key, value in headers.items()}
        self.assertEqual(lowered["x-strij-resources-cpu"], "2")

    def test_http_failure_maps_to_remote_error(self) -> None:
        self.server.mode = "fail:429"

        @strij.remote
        def rejected():
            return 1

        with self.assertRaises(_runtime.RemoteError) as raised:
            rejected.remote().get(timeout=5)
        self.assertEqual(raised.exception.status, _runtime.TaskStatus.CAPACITY_REFUSED)
        self.assertIn("server says no", str(raised.exception))

    def test_unavailable_http_status_maps_to_unavailable(self) -> None:
        self.server.mode = "fail:503"

        @strij.remote
        def unavailable():
            return 1

        with self.assertRaises(_runtime.RemoteError) as raised:
            unavailable.remote().get(timeout=5)
        self.assertEqual(raised.exception.status, _runtime.TaskStatus.UNAVAILABLE)

    def test_get_timeout_does_not_cancel_the_task(self) -> None:
        self.server.mode = "slow"

        @strij.remote
        def eventually():
            return 42

        ref = eventually.remote()
        with self.assertRaises(_runtime.RemoteTimeoutError):
            ref.get(timeout=0.05)
        # The HTTP thread keeps running; the ref still resolves.
        self.assertEqual(ref.get(timeout=5), 42)


if __name__ == "__main__":
    unittest.main()
