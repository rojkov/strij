#!/usr/bin/env python3
"""Unit tests for tools/dev_cluster.py (stdlib unittest, no external deps)."""

from __future__ import annotations

import os
import queue
import socket
import sys
import tempfile
import textwrap
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import dev_cluster


NODE_CONFIG = """\
tlv_listener:
  address: "127.0.0.1"
  port: {port}
"""

GATEWAY_CONFIG = """\
http_listener:
  address: "127.0.0.1"
  port: {port}
"""


def write(path: Path, content: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content), encoding="utf-8")
    return path


def write_preset(
    presets_dir: Path,
    name: str,
    *,
    nodes: int = 2,
    gateways: int = 1,
    node_port: int = 9090,
    gateway_port: int = 8081,
    readiness: str | None = "echo",
) -> Path:
    directory = presets_dir / name
    directory.mkdir(parents=True, exist_ok=True)
    lines = [
        f'description: "{name} test preset"',
        "",
    ]
    if readiness is not None:
        lines.extend(["readiness:", f'  task_type: "{readiness}"', ""])
    lines.append("processes:")
    for index in range(nodes):
        lines.extend(
            [
                f'  - name: "node-{index}"',
                '    role: "node"',
                f'    config: "node-{index}.yaml"',
            ]
        )
    for index in range(gateways):
        lines.extend(
            [
                f'  - name: "gateway-{index}"',
                '    role: "gateway"',
                f'    config: "gateway-{index}.yaml"',
            ]
        )
    write(directory / "cluster.yaml", "\n".join(lines) + "\n")
    for index in range(nodes):
        write(directory / f"node-{index}.yaml", NODE_CONFIG.format(port=node_port + index))
    for index in range(gateways):
        write(
            directory / f"gateway-{index}.yaml",
            GATEWAY_CONFIG.format(port=gateway_port + index),
        )
    return directory


def unused_pid() -> int:
    for candidate in range(999_999, 1_000_999):
        if not Path(f"/proc/{candidate}").exists():
            return candidate
    raise AssertionError("could not find an unused pid")


class PresetParsingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.presets_dir = self.root / "config" / "examples"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_manifest_lists_processes_in_boot_order(self) -> None:
        write_preset(self.presets_dir, "local")
        preset = dev_cluster.load_preset("local", self.presets_dir)

        self.assertEqual(
            [process.name for process in preset.processes],
            ["node-0", "node-1", "gateway-0"],
        )
        self.assertEqual(
            [process.role for process in preset.processes],
            ["node", "node", "gateway"],
        )
        self.assertEqual([process.port for process in preset.processes], [9090, 9091, 8081])
        self.assertEqual(preset.readiness_task_type, "echo")
        self.assertEqual(preset.description, "local test preset")

    def test_both_listener_shapes(self) -> None:
        write_preset(self.presets_dir, "local")
        preset = dev_cluster.load_preset("local", self.presets_dir)

        node = preset.process("node-0")
        gateway = preset.process("gateway-0")
        self.assertEqual((node.address, node.port), ("127.0.0.1", 9090))
        self.assertEqual((gateway.address, gateway.port), ("127.0.0.1", 8081))

    def test_quoted_port_is_accepted(self) -> None:
        directory = write_preset(self.presets_dir, "local", nodes=1, gateways=1)
        write(directory / "node-0.yaml", 'tlv_listener:\n  port: "9191"\n')
        preset = dev_cluster.load_preset("local", self.presets_dir)
        self.assertEqual(preset.process("node-0").port, 9191)

    def test_missing_port_names_the_process(self) -> None:
        directory = write_preset(self.presets_dir, "local", nodes=1, gateways=1)
        write(directory / "node-0.yaml", 'tlv_listener:\n  address: "127.0.0.1"\n')

        with self.assertRaises(dev_cluster.DevClusterError) as context:
            dev_cluster.load_preset("local", self.presets_dir)
        message = str(context.exception)
        self.assertIn("node-0", message)
        self.assertIn("tlv_listener.port", message)

    def test_wrong_listener_shape_is_rejected(self) -> None:
        directory = write_preset(self.presets_dir, "local", nodes=1, gateways=1)
        write(directory / "gateway-0.yaml", 'tlv_listener:\n  port: 8081\n')

        with self.assertRaises(dev_cluster.DevClusterError) as context:
            dev_cluster.load_preset("local", self.presets_dir)
        self.assertIn("http_listener.port", str(context.exception))

    def test_invalid_role_names_the_offending_entry(self) -> None:
        directory = write_preset(self.presets_dir, "local", nodes=1, gateways=1)
        write(
            directory / "cluster.yaml",
            """\
            processes:
              - name: "worker-0"
                role: "worker"
                config: "node-0.yaml"
            """,
        )

        with self.assertRaises(dev_cluster.DevClusterError) as context:
            dev_cluster.load_preset("local", self.presets_dir)
        message = str(context.exception)
        self.assertIn("worker-0", message)
        self.assertIn("invalid role", message)

    def test_missing_config_names_the_offending_entry(self) -> None:
        directory = write_preset(self.presets_dir, "local", nodes=1, gateways=1)
        (directory / "node-0.yaml").unlink()

        with self.assertRaises(dev_cluster.DevClusterError) as context:
            dev_cluster.load_preset("local", self.presets_dir)
        message = str(context.exception)
        self.assertIn("node-0", message)
        self.assertIn("missing config", message)

    def test_duplicate_ports_are_rejected(self) -> None:
        directory = write_preset(self.presets_dir, "local", nodes=2, gateways=1)
        write(directory / "node-1.yaml", NODE_CONFIG.format(port=9090))

        with self.assertRaises(dev_cluster.DevClusterError) as context:
            dev_cluster.load_preset("local", self.presets_dir)
        self.assertIn("both use port 9090", str(context.exception))

    def test_missing_preset_lists_available_and_scaffold(self) -> None:
        write_preset(self.presets_dir, "local")

        with self.assertRaises(dev_cluster.DevClusterError) as context:
            dev_cluster.load_preset("missing", self.presets_dir)
        message = str(context.exception)
        self.assertIn("local", message)
        self.assertIn("scaffold missing", message)

    def test_unknown_manifest_key_is_rejected(self) -> None:
        directory = write_preset(self.presets_dir, "local", nodes=1, gateways=1)
        (directory / "cluster.yaml").write_text(
            "processes:\n  - name: node-0\n    role: node\n    config: node-0.yaml\n"
            "  - name: node-1\n    role: node\n    config: node-1.yaml\n"
            "    colour: blue\n",
            encoding="utf-8",
        )

        with self.assertRaises(dev_cluster.DevClusterError) as context:
            dev_cluster.load_preset("local", self.presets_dir)
        self.assertIn("unknown process key 'colour'", str(context.exception))


class StateTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.state_root = Path(self.temporary.name) / ".devcluster"
        self.process = {
            "name": "node-0",
            "role": "node",
            "pid": os.getpid(),
            "pgid": os.getpid(),
            "port": 9090,
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_missing_state(self) -> None:
        self.assertIsNone(dev_cluster.read_state("local", self.state_root))
        self.assertEqual(
            dev_cluster.classify_state(None), "missing"
        )

    def test_write_read_round_trip(self) -> None:
        state = {
            "name": "local",
            "processes": [self.process],
            "preset_dir": "config/examples/local",
        }
        dev_cluster.write_state("local", state, self.state_root)
        loaded = dev_cluster.read_state("local", self.state_root)

        self.assertIsNotNone(loaded)
        assert loaded is not None
        self.assertEqual(loaded["name"], "local")
        self.assertEqual(loaded["processes"], [self.process])
        self.assertEqual(loaded["preset_dir"], "config/examples/local")
        self.assertIn("updated_at", loaded)

        # Atomic replacement must not leave temporary files behind.
        leftovers = [
            path.name
            for path in dev_cluster.cluster_state_dir("local", self.state_root).iterdir()
            if path.name.startswith(".state.")
        ]
        self.assertEqual(leftovers, [])

    def test_live_pid_is_live(self) -> None:
        state = {"processes": [self.process]}
        self.assertEqual(dev_cluster.classify_state(state), "live")
        self.assertTrue(dev_cluster.process_alive(os.getpid()))

    def test_unused_pid_is_stale(self) -> None:
        pid = unused_pid()
        state = {"processes": [{**self.process, "pid": pid, "pgid": pid}]}
        self.assertEqual(dev_cluster.classify_state(state), "stale")
        self.assertFalse(dev_cluster.process_alive(pid))

    def test_stopped_state_wins_over_dead_pids(self) -> None:
        state = {
            "processes": [{**self.process, "pid": unused_pid()}],
            "stopped_at": "2026-01-01T00:00:00+00:00",
        }
        self.assertEqual(dev_cluster.classify_state(state), "stopped")

    def test_flock_serializes_a_name(self) -> None:
        with dev_cluster.cluster_lock("local", self.state_root):
            with self.assertRaises(dev_cluster.DevClusterError):
                with dev_cluster.cluster_lock("local", self.state_root):
                    pass

    def test_plausible_pid_that_does_not_exist(self) -> None:
        state = {"processes": [{"pid": 0}]}
        self.assertEqual(dev_cluster.classify_state(state), "stale")


class PortCheckTest(unittest.TestCase):
    def test_listener_is_not_free(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            port = listener.getsockname()[1]
            self.assertFalse(dev_cluster.port_is_free("127.0.0.1", port))

        self.assertTrue(dev_cluster.port_is_free("127.0.0.1", port))


class TailTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_tail_reports_end_offset(self) -> None:
        path = self.root / "node-0.log"
        path.write_text("one\ntwo\nthree\n", encoding="utf-8")

        lines, offset = dev_cluster._tail_lines(path, 2)

        self.assertEqual(lines, ["two", "three"])
        self.assertEqual(offset, len(path.read_bytes()))

    def test_follow_reader_starts_after_the_tail(self) -> None:
        path = self.root / "node-0.log"
        path.write_text("old\n", encoding="utf-8")
        _, offset = dev_cluster._tail_lines(path, 10)

        with path.open("a", encoding="utf-8") as stream:
            stream.write("new\n")

        queue_ = queue.Queue()
        stop_event = threading.Event()
        thread = threading.Thread(
            target=dev_cluster._follow_reader,
            args=("node-0", path, offset, queue_, stop_event),
            daemon=True,
        )
        thread.start()
        try:
            self.assertEqual(queue_.get(timeout=2.0), ("node-0", "new"))
            self.assertTrue(queue_.empty())
        finally:
            stop_event.set()
            thread.join(timeout=2.0)

    def test_missing_log_has_no_tail_and_zero_offset(self) -> None:
        self.assertEqual(
            dev_cluster._tail_lines(self.root / "missing.log", 10), (None, 0)
        )

    def test_empty_log_follows_from_the_start(self) -> None:
        path = self.root / "empty.log"
        path.write_text("", encoding="utf-8")

        self.assertEqual(dev_cluster._tail_lines(path, 10), ([], 0))


class ScaffoldTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.presets_dir = self.root / "config" / "examples"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def read(self, name: str, filename: str) -> str:
        return (self.presets_dir / name / filename).read_text(encoding="utf-8")

    def test_default_topology(self) -> None:
        directory = dev_cluster.scaffold_preset("local", presets_dir=self.presets_dir)
        self.assertTrue(directory.is_dir())

        preset = dev_cluster.load_preset("local", self.presets_dir)
        self.assertEqual(
            [process.name for process in preset.processes],
            ["node-0", "node-1", "gateway-0"],
        )
        self.assertEqual(
            [process.port for process in preset.processes], [9090, 9091, 8081]
        )

        gateway = self.read("local", "gateway-0.yaml")
        self.assertIn('name: "probe"', gateway)
        self.assertIn("candidate_count: 2", gateway)
        self.assertIn("probe_deadline_ms: 1000", gateway)
        self.assertIn('"127.0.0.1:9090"', gateway)
        self.assertIn('"127.0.0.1:9091"', gateway)
        self.assertIn("summary_mesh", gateway)
        self.assertIn('gateway_id: "local-gateway-0"', gateway)
        self.assertIn('level: "debug"', gateway)

        for node in ("node-0.yaml", "node-1.yaml"):
            content = self.read("local", node)
            self.assertIn('name: "probe"', content)
            self.assertIn('name: "push"', content)
            self.assertIn('name: "default"', content)
            self.assertIn("local_default: true", content)
            self.assertIn('level: "debug"', content)

    def test_every_gateway_discovers_every_node(self) -> None:
        dev_cluster.scaffold_preset(
            "big", presets_dir=self.presets_dir, nodes=3, gateways=2
        )
        preset = dev_cluster.load_preset("big", self.presets_dir)
        self.assertEqual(len(preset.processes), 5)

        gateway_ids = set()
        for index in range(2):
            content = self.read("big", f"gateway-{index}.yaml")
            for port in (9090, 9091, 9092):
                self.assertIn(f'"127.0.0.1:{port}"', content)
            gateway_ids.add(f"big-gateway-{index}")
            self.assertIn(f'gateway_id: "big-gateway-{index}"', content)
        self.assertEqual(len(gateway_ids), 2)

    def test_scheduler_can_be_overridden(self) -> None:
        dev_cluster.scaffold_preset(
            "alt", presets_dir=self.presets_dir, scheduler="capability_aware"
        )
        gateway = self.read("alt", "gateway-0.yaml")
        self.assertIn('name: "capability_aware"', gateway)
        self.assertIn("CapabilityAwareSchedulerConfig", gateway)

        dev_cluster.scaffold_preset(
            "round", presets_dir=self.presets_dir, gateway_port=8281, node_port=9290,
            scheduler="round_robin",
        )
        gateway = self.read("round", "gateway-0.yaml")
        self.assertIn('name: "round_robin"', gateway)
        # Node configs always advertise both policies.
        node = self.read("round", "node-0.yaml")
        self.assertIn('name: "probe"', node)
        self.assertIn('name: "push"', node)

    def test_event_plane_can_be_omitted(self) -> None:
        dev_cluster.scaffold_preset(
            "plain", presets_dir=self.presets_dir, event_plane=False
        )
        gateway = self.read("plain", "gateway-0.yaml")
        self.assertNotIn("event_plane", gateway)
        self.assertNotIn("summary_mesh", gateway)

    def test_existing_preset_requires_force(self) -> None:
        dev_cluster.scaffold_preset("local", presets_dir=self.presets_dir)
        with self.assertRaises(dev_cluster.DevClusterError):
            dev_cluster.scaffold_preset("local", presets_dir=self.presets_dir)

        dev_cluster.scaffold_preset("local", presets_dir=self.presets_dir, force=True)
        self.assertTrue((self.presets_dir / "local" / "cluster.yaml").is_file())

    def test_empty_topology_is_rejected_without_writing(self) -> None:
        with self.assertRaises(dev_cluster.DevClusterError):
            dev_cluster.scaffold_preset("empty", presets_dir=self.presets_dir, nodes=0)
        self.assertFalse((self.presets_dir / "empty").exists())

        with self.assertRaises(dev_cluster.DevClusterError):
            dev_cluster.scaffold_preset(
                "empty", presets_dir=self.presets_dir, gateways=0
            )
        self.assertFalse((self.presets_dir / "empty").exists())

    def test_colliding_port_range_is_rejected_with_a_suggestion(self) -> None:
        dev_cluster.scaffold_preset("local", presets_dir=self.presets_dir)

        with self.assertRaises(dev_cluster.DevClusterError) as context:
            dev_cluster.scaffold_preset("other", presets_dir=self.presets_dir)
        message = str(context.exception)
        self.assertIn("already used by preset 'local'", message)
        self.assertIn("--gateway-port", message)
        self.assertIn("--node-port", message)
        self.assertFalse((self.presets_dir / "other").exists())

    def test_force_can_rewrite_its_own_port_range(self) -> None:
        dev_cluster.scaffold_preset("local", presets_dir=self.presets_dir)
        dev_cluster.scaffold_preset(
            "local", presets_dir=self.presets_dir, force=True, nodes=3, gateways=2
        )
        preset = dev_cluster.load_preset("local", self.presets_dir)
        self.assertEqual(len(preset.processes), 5)

        # Shrinking the topology drops the files that no longer belong to it.
        dev_cluster.scaffold_preset(
            "local", presets_dir=self.presets_dir, force=True, nodes=1, gateways=1
        )
        self.assertFalse((self.presets_dir / "local" / "node-1.yaml").exists())
        self.assertFalse((self.presets_dir / "local" / "node-2.yaml").exists())
        self.assertFalse((self.presets_dir / "local" / "gateway-1.yaml").exists())
        self.assertTrue((self.presets_dir / "local" / "node-0.yaml").is_file())

    def test_task_handlers_are_written(self) -> None:
        dev_cluster.scaffold_preset("local", presets_dir=self.presets_dir)
        node = self.read("local", "node-0.yaml")
        self.assertIn('name: "echo"', node)
        self.assertIn('name: "piped_executable"', node)
        self.assertIn('name: "openworkflow"', node)

    def test_invalid_preset_name_is_rejected(self) -> None:
        with self.assertRaises(dev_cluster.DevClusterError):
            dev_cluster.scaffold_preset("../escape", presets_dir=self.presets_dir)


if __name__ == "__main__":
    unittest.main()
