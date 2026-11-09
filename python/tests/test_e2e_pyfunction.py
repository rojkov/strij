import glob
import json
import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request

import strij
from strij import _runtime


def runfile(relative: str) -> str:
    srcdir = os.environ.get("TEST_SRCDIR", "")
    workspace = os.environ.get("TEST_WORKSPACE", "_main")
    return os.path.join(srcdir, workspace, relative)


def worker_available() -> bool:
    path = runfile("python/worker")
    # The worker launcher and its runfiles carry Python, cloudpickle, and the
    # protobuf runtime. When any of them are absent (for example a stripped
    # deployment image), the real-Python e2e suite skips with this reason
    # instead of failing on host state.
    if not (os.path.isfile(path) and os.access(path, os.X_OK)):
        return False
    srcdir = os.environ.get("TEST_SRCDIR", "")
    if not os.path.isfile(runfile("third_party/python/cloudpickle/__init__.py")):
        return False
    if not os.path.isfile(
        os.path.join(srcdir, "protobuf+", "python", "google", "protobuf", "__init__.py")
    ):
        return False
    return bool(
        glob.glob(os.path.join(srcdir, "rules_python++python+python_*", "bin", "python3"))
    )


def allocate_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_port(port: int, timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.02)
    raise RuntimeError("port %d did not start accepting connections" % port)


def http_json(port: int, body: dict, headers: dict) -> tuple[int, bytes]:
    data = json.dumps(body).encode("utf-8")
    request = urllib.request.Request(
        "http://127.0.0.1:%d/tasks/pyfunction" % port,
        data=data,
        headers={"Content-Type": "application/json", **headers},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read()


class Node:
    def __init__(self, name: str, directory: str, concurrency: int, worker: str) -> None:
        self.name = name
        self.directory = directory
        self.port = allocate_port()
        self.concurrency = concurrency
        self.worker = worker
        self.process = None
        self.log_path = os.path.join(directory, "node.log")

    def write_config(self) -> str:
        config = """
tlv_listener:
  address: "127.0.0.1"
  port: {port}

schedulers:
  # "push" advertises the wire protocol the gateway's round-robin scheduler
  # matches on; "default" owns local admission and forwarding.
  - extension:
      name: "push"
  - extension:
      name: "default"
    local_default: true

pools:
  - name: "cpu"
    total: 16

task_handlers:
  - name: "pyfunction"
    typed_config:
      "@type": "type.googleapis.com/strij.extensions.task_handlers.pyfunction.PyFunctionTaskHandlerConfig"
      capacity:
        concurrency: {concurrency}
      worker_executable: "{worker}"
      startup_timeout:
        seconds: 20
      task_timeout:
        seconds: 0
      remote_call_timeout:
        seconds: 20
      max_frame_bytes: 8388608

heartbeat_interval:
  seconds: 1

logging:
  level: "debug"
  format: "text"
  output: "stderr"
""".format(port=self.port, concurrency=self.concurrency, worker=self.worker)
        path = os.path.join(self.directory, "nodeagent.yaml")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(config)
        return path


class Gateway:
    def __init__(self, name: str, directory: str, nodes: list[Node]) -> None:
        self.name = name
        self.directory = directory
        self.nodes = nodes
        self.port = allocate_port()
        self.process = None
        self.log_path = os.path.join(directory, "gateway.log")

    def write_config(self) -> str:
        addresses = "\n".join(
            '      - "127.0.0.1:%d"' % node.port for node in self.nodes
        )
        config = """
http_listener:
  address: "127.0.0.1"
  port: {port}

node_discovery:
  name: "static"
  typed_config:
    "@type": "type.googleapis.com/strij.config.StaticNodeDiscoveryConfig"
    addresses:
{addresses}

schedulers:
  - extension:
      name: "round_robin"

logging:
  level: "debug"
  format: "text"
  output: "stderr"
""".format(port=self.port, addresses=addresses)
        path = os.path.join(self.directory, "gateway.yaml")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(config)
        return path


class Cluster:
    def __init__(self, concurrencies: list[int]) -> None:
        self.root = tempfile.mkdtemp(prefix="strij-py-e2e-")
        worker = runfile("python/worker")
        self.nodes = []
        for index, concurrency in enumerate(concurrencies):
            directory = os.path.join(self.root, "node%d" % index)
            os.makedirs(directory)
            self.nodes.append(Node("node%d" % index, directory, concurrency, worker))
        gateway_directory = os.path.join(self.root, "gateway0")
        os.makedirs(gateway_directory)
        self.gateway = Gateway("gateway0", gateway_directory, self.nodes)
        self.processes = []

    def _spawn(self, executable: str, config: str, directory: str, log_path: str) -> None:
        log = open(log_path, "wb")
        process = subprocess.Popen(
            [executable, "--config_file=" + config],
            cwd=directory,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.processes.append((process, log))

    def start(self) -> None:
        for node in self.nodes:
            self._spawn(
                runfile("src/nodeagent/exe/nodeagent"),
                node.write_config(),
                node.directory,
                node.log_path,
            )
        for node in self.nodes:
            wait_for_port(node.port)
        self._spawn(
            runfile("src/gateway/exe/gateway"),
            self.gateway.write_config(),
            self.gateway.directory,
            self.gateway.log_path,
        )
        wait_for_port(self.gateway.port)
        self._wait_for_pyfunction()

    def _wait_for_pyfunction(self) -> None:
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            status, body = http_json(
                self.gateway.port,
                {"python_code": "def main(value):\n    return value", "input": 1},
                {"X-Strij-Function": "embedded:main"},
            )
            if status == 200 and body == b"1":
                return
            time.sleep(0.05)
        raise RuntimeError("gateway did not accept a pyfunction probe\n" + self.diagnostics())

    def diagnostics(self) -> str:
        chunks = []
        for node in self.nodes:
            try:
                with open(node.log_path, "r", encoding="utf-8", errors="replace") as handle:
                    chunks.append("--- %s ---\n%s" % (node.name, handle.read()[-4000:]))
            except OSError:
                pass
        try:
            with open(self.gateway.log_path, "r", encoding="utf-8", errors="replace") as handle:
                chunks.append("--- gateway ---\n%s" % handle.read()[-4000:])
        except OSError:
            pass
        return "\n".join(chunks)

    def stop(self) -> None:
        for process, _log in self.processes:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
        deadline = time.monotonic() + 5
        for process, _log in self.processes:
            try:
                process.wait(timeout=max(0.1, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=5)
        for _process, log in self.processes:
            log.close()


class PyFunctionE2ETest(unittest.TestCase):
    def setUp(self) -> None:
        if not worker_available():
            self.skipTest(
                "the Bazel pyfunction worker (Python + cloudpickle + protobuf "
                "runfiles) is unavailable; skipping the real-Python e2e suite"
            )
        self.clusters: list[Cluster] = []

    def tearDown(self) -> None:
        for cluster in self.clusters:
            cluster.stop()

    def start_cluster(self, concurrencies: list[int]) -> Cluster:
        cluster = Cluster(concurrencies)
        cluster.start()
        self.clusters.append(cluster)
        return cluster

    def test_client_pickle_success_and_task_failure(self) -> None:
        cluster = self.start_cluster([2])
        strij.init("http://127.0.0.1:%d" % cluster.gateway.port)

        @strij.remote
        def add(left, right):
            return left + right

        self.assertEqual(add.remote(2, 5).get(timeout=20), 7)

        @strij.remote
        def explode():
            raise ValueError("kaboom")

        with self.assertRaises(_runtime.RemoteError) as raised:
            explode.remote().get(timeout=20)
        self.assertEqual(raised.exception.status, _runtime.TaskStatus.INTERNAL)
        self.assertIn("kaboom", str(raised.exception))

    def test_large_pickle_body_round_trip(self) -> None:
        cluster = self.start_cluster([1])
        strij.init("http://127.0.0.1:%d" % cluster.gateway.port)

        @strij.remote
        def size(payload):
            return len(payload)

        # Larger than a pipe buffer: the handler must resume a partial write
        # instead of stalling the task forever.
        payload = b"x" * (256 * 1024)
        self.assertEqual(size.remote(payload).get(timeout=20), len(payload))

    def test_reference_json_mode(self) -> None:
        cluster = self.start_cluster([2])
        status, body = http_json(
            cluster.gateway.port,
            {
                "python_code": "def main(value):\n    return value * 3",
                "input": 14,
            },
            {"X-Strij-Function": "embedded:main"},
        )
        self.assertEqual(status, 200, cluster.diagnostics())
        self.assertEqual(json.loads(body), 42)

    def test_nested_remote_call_locally(self) -> None:
        cluster = self.start_cluster([2])
        strij.init("http://127.0.0.1:%d" % cluster.gateway.port)

        @strij.remote
        def child(left, right):
            return left + right

        @strij.remote
        def parent():
            return child.remote(3, 4).get() + 1

        self.assertEqual(parent.remote().get(timeout=20), 8)

    def test_nested_remote_call_carries_requirements(self) -> None:
        cluster = self.start_cluster([2])
        strij.init("http://127.0.0.1:%d" % cluster.gateway.port)

        @strij.remote(requirements={"cpu": 1})
        def child():
            return 1

        @strij.remote
        def parent():
            return child.remote().get()

        self.assertEqual(parent.remote().get(timeout=20), 1)

    def test_nested_remote_call_is_forwarded(self) -> None:
        cluster = self.start_cluster([1, 2])
        strij.init("http://127.0.0.1:%d" % cluster.gateway.port)

        @strij.remote
        def where():
            return os.getcwd()

        @strij.remote
        def parent():
            return (os.getcwd(), where.remote().get())

        observed = None
        for _ in range(8):
            parent_cwd, child_cwd = parent.remote().get(timeout=20)
            if parent_cwd.endswith("node0"):
                observed = (parent_cwd, child_cwd)
                break
        self.assertIsNotNone(
            observed,
            "no parent task landed on node0\n" + cluster.diagnostics(),
        )
        self.assertTrue(
            observed[1].endswith("node1"),
            "the child did not run on node1: %r\n%s"
            % (observed, cluster.diagnostics()),
        )

    def test_capacity_exhaustion_is_refused(self) -> None:
        cluster = self.start_cluster([1])
        strij.init("http://127.0.0.1:%d" % cluster.gateway.port)

        @strij.remote
        def hold():
            time.sleep(2)
            return "done"

        first = hold.remote()
        time.sleep(0.3)
        with self.assertRaises(_runtime.RemoteError) as raised:
            hold.remote().get(timeout=10)
        self.assertEqual(
            raised.exception.status,
            _runtime.TaskStatus.CAPACITY_REFUSED,
            cluster.diagnostics(),
        )
        self.assertEqual(first.get(timeout=20), "done")

    def test_worker_crash_fails_task_and_recovers(self) -> None:
        cluster = self.start_cluster([1])
        strij.init("http://127.0.0.1:%d" % cluster.gateway.port)

        @strij.remote
        def crash():
            os._exit(7)

        with self.assertRaises(_runtime.RemoteError) as raised:
            crash.remote().get(timeout=20)
        self.assertEqual(raised.exception.status, _runtime.TaskStatus.INTERNAL)

        @strij.remote
        def healthy():
            return "alive"

        # The slot respawns and serves the next task instead of staying dead.
        self.assertEqual(healthy.remote().get(timeout=20), "alive")


if __name__ == "__main__":
    unittest.main()
