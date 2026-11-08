#!/usr/bin/env python3
"""Local development cluster lifecycle tool.

A preset is a committed directory under ``config/examples/<name>/`` holding a
``cluster.yaml`` manifest plus one ordinary gateway or node agent YAML per
process.  The launcher reads each process's listener port from its own config;
the manifest only fixes the process set and the boot order.

Runtime state (logs and ``state.json``) lives under ``.devcluster/<name>/``.
Presets are read-only to every command except ``scaffold``.
"""

from __future__ import annotations

import argparse
import contextlib
import datetime
import fcntl
import json
import os
import queue
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PRESETS_DIR = REPO_ROOT / "config" / "examples"
STATE_ROOT = REPO_ROOT / ".devcluster"

NODE_ROLE = "node"
GATEWAY_ROLE = "gateway"
ROLES = (NODE_ROLE, GATEWAY_ROLE)

LISTENER_BLOCKS = {
    NODE_ROLE: "tlv_listener",
    GATEWAY_ROLE: "http_listener",
}

DEFAULT_READINESS_TASK_TYPE = "echo"
DEFAULT_NODE_PORT = 9090
DEFAULT_GATEWAY_PORT = 8081
DEFAULT_NODES = 2
DEFAULT_GATEWAYS = 1
DEFAULT_SCHEDULER = "probe"
SCHEDULERS = ("probe", "capability_aware", "round_robin")
DEFAULT_HANDLERS = ("echo", "piped_executable", "openworkflow")

GATEWAY_BINARY = Path("bazel-bin/src/gateway/exe/gateway")
NODEAGENT_BINARY = Path("bazel-bin/src/nodeagent/exe/nodeagent")
BUILD_HINT = (
    "Build the binaries with: "
    "bazel build //src/gateway/exe:gateway //src/nodeagent/exe:nodeagent"
)

DEFAULT_GRACE_SECONDS = 5.0
PORT_CHECK_TIMEOUT_SECONDS = 2.0
READINESS_TIMEOUT_SECONDS = 30.0
LOG_TAIL_LINES = 40

NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]*$")


class DevClusterError(RuntimeError):
    """A user-facing dev cluster failure."""


@dataclass(frozen=True)
class ProcessSpec:
    name: str
    role: str
    config: Path
    address: str
    port: int


@dataclass(frozen=True)
class Preset:
    name: str
    directory: Path
    description: str
    readiness_task_type: str
    processes: tuple[ProcessSpec, ...]

    def process(self, name: str) -> ProcessSpec:
        for process in self.processes:
            if process.name == name:
                return process
        known = ", ".join(process.name for process in self.processes) or "(none)"
        raise DevClusterError(
            f"preset '{self.name}' has no process '{name}'; known processes: {known}"
        )


# ---------------------------------------------------------------------------
# A deliberately small YAML subset reader.  Manifests and listener blocks are
# authored by this tool or by hand in a flat shape; using a YAML library would
# add a third-party dependency for ~60 lines of scanning.
# ---------------------------------------------------------------------------


def _strip_comment(line: str) -> str:
    """Remove a trailing YAML comment while respecting simple quoting."""
    in_single = False
    in_double = False
    result = []
    for char in line:
        if char == "'" and not in_double:
            in_single = not in_single
        elif char == '"' and not in_single:
            in_double = not in_double
        elif char == "#" and not in_single and not in_double:
            break
        result.append(char)
    return "".join(result)


def _unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
        return value[1:-1]
    return value


def _manifest_error(path: Path, line_number: int, message: str) -> DevClusterError:
    return DevClusterError(f"{path}:{line_number}: {message}")


def parse_manifest(path: Path) -> dict:
    """Parse ``cluster.yaml`` into description/readiness/processes."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise DevClusterError(f"cannot read manifest '{path}': {error}") from error

    description = ""
    readiness: dict[str, str] = {}
    processes: list[dict[str, str]] = []
    section: str | None = None
    current: dict[str, str] | None = None

    for line_number, raw in enumerate(lines, start=1):
        line = _strip_comment(raw).rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        text = line.strip()

        if indent == 0:
            if text.startswith("-"):
                raise _manifest_error(path, line_number, "unexpected list item")
            key, separator, value = text.partition(":")
            key = key.strip()
            if not separator or not key:
                raise _manifest_error(path, line_number, f"expected 'key: value', got '{text}'")
            section = key
            current = None
            if key == "description":
                description = _unquote(value)
            elif key == "readiness":
                if value.strip():
                    raise _manifest_error(path, line_number, "readiness must be a block")
                readiness = {}
            elif key == "processes":
                if value.strip():
                    raise _manifest_error(path, line_number, "processes must be a list")
                processes = []
            else:
                raise _manifest_error(path, line_number, f"unknown manifest key '{key}'")
            continue

        if section in (None, "description"):
            raise _manifest_error(path, line_number, "unexpected indented line")

        if section == "readiness":
            key, separator, value = text.partition(":")
            key = key.strip()
            if not separator or not key:
                raise _manifest_error(path, line_number, f"expected 'key: value', got '{text}'")
            if key != "task_type":
                raise _manifest_error(path, line_number, f"unknown readiness key '{key}'")
            readiness["task_type"] = _unquote(value)
            continue

        if section == "processes":
            if text.startswith("-"):
                current = {}
                processes.append(current)
                text = text[1:].strip()
                if not text:
                    continue
            if current is None:
                raise _manifest_error(path, line_number, "process entry must start with '-'")
            key, separator, value = text.partition(":")
            key = key.strip()
            if not separator or not key:
                raise _manifest_error(path, line_number, f"expected 'key: value', got '{text}'")
            if key not in ("name", "role", "config"):
                raise _manifest_error(path, line_number, f"unknown process key '{key}'")
            current[key] = _unquote(value)
            continue

        raise _manifest_error(path, line_number, f"unexpected line under '{section}'")

    return {
        "description": description,
        "readiness": readiness,
        "processes": processes,
    }


def read_listener(config_path: Path, role: str) -> tuple[str, int]:
    """Read ``(address, port)`` from a node/gateway listener block."""
    block = LISTENER_BLOCKS[role]
    try:
        lines = config_path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise DevClusterError(f"cannot read config '{config_path}': {error}") from error

    in_block = False
    block_indent = 0
    address = "127.0.0.1"
    port: int | None = None

    for raw in lines:
        line = _strip_comment(raw).rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        text = line.strip()

        if not in_block:
            if indent == 0 and text == f"{block}:":
                in_block = True
                block_indent = indent
            continue

        if indent <= block_indent:
            break

        key, separator, value = text.partition(":")
        if not separator:
            continue
        key = key.strip()
        if key == "address":
            address = _unquote(value)
        elif key == "port":
            raw_port = _unquote(value)
            try:
                port = int(raw_port)
            except ValueError as error:
                raise DevClusterError(
                    f"config '{config_path}': {block}.port is not an integer "
                    f"(got '{raw_port}')"
                ) from error

    if port is None:
        raise DevClusterError(
            f"config '{config_path}': no {block}.port block found "
            f"(expected '{block}:' with an indented 'port: N')"
        )
    if port < 1 or port > 65535:
        raise DevClusterError(
            f"config '{config_path}': {block}.port {port} is outside 1..65535"
        )
    return address, port


def available_preset_names(presets_dir: Path = PRESETS_DIR) -> list[str]:
    if not presets_dir.is_dir():
        return []
    return sorted(
        path.parent.name for path in presets_dir.glob("*/cluster.yaml") if path.is_file()
    )


def load_preset(name: str, presets_dir: Path = PRESETS_DIR) -> Preset:
    directory = presets_dir / name
    manifest_path = directory / "cluster.yaml"
    if not manifest_path.is_file():
        available = ", ".join(available_preset_names(presets_dir)) or "(none)"
        raise DevClusterError(
            f"preset '{name}' not found in {presets_dir}; available presets: {available}. "
            f"Create one with: python3 tools/dev_cluster.py scaffold {name}"
        )

    data = parse_manifest(manifest_path)
    processes: list[ProcessSpec] = []
    seen_names: set[str] = set()
    seen_ports: dict[int, str] = {}

    for index, entry in enumerate(data["processes"]):
        label = entry.get("name") or f"process #{index + 1}"
        missing = [key for key in ("name", "role", "config") if not entry.get(key)]
        if missing:
            raise DevClusterError(
                f"preset '{name}': process '{label}' is missing "
                f"{', '.join(missing)} in {manifest_path}"
            )

        process_name = entry["name"]
        role = entry["role"]
        if role not in ROLES:
            raise DevClusterError(
                f"preset '{name}': process '{process_name}' has invalid role '{role}' "
                f"(expected one of: {', '.join(ROLES)})"
            )
        if process_name in seen_names:
            raise DevClusterError(
                f"preset '{name}': duplicate process name '{process_name}'"
            )
        seen_names.add(process_name)

        config = Path(entry["config"])
        if not config.is_absolute():
            config = directory / config
        if not config.is_file():
            raise DevClusterError(
                f"preset '{name}': process '{process_name}' references missing config "
                f"'{entry['config']}'"
            )

        try:
            address, port = read_listener(config, role)
        except DevClusterError as error:
            raise DevClusterError(
                f"preset '{name}': process '{process_name}': {error}"
            ) from error

        if port in seen_ports:
            raise DevClusterError(
                f"preset '{name}': processes '{seen_ports[port]}' and '{process_name}' "
                f"both use port {port}"
            )
        seen_ports[port] = process_name

        processes.append(
            ProcessSpec(
                name=process_name,
                role=role,
                config=config,
                address=address,
                port=port,
            )
        )

    if not processes:
        raise DevClusterError(f"preset '{name}': manifest lists no processes")

    readiness = data["readiness"].get("task_type", DEFAULT_READINESS_TASK_TYPE)
    return Preset(
        name=name,
        directory=directory,
        description=data["description"],
        readiness_task_type=readiness,
        processes=tuple(processes),
    )


def discover_presets(presets_dir: Path = PRESETS_DIR) -> list[Preset]:
    return [load_preset(name, presets_dir) for name in available_preset_names(presets_dir)]


# ---------------------------------------------------------------------------
# Runtime state
# ---------------------------------------------------------------------------


def now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")


def cluster_state_dir(name: str, state_root: Path = STATE_ROOT) -> Path:
    return state_root / name


def cluster_state_path(name: str, state_root: Path = STATE_ROOT) -> Path:
    return cluster_state_dir(name, state_root) / "state.json"


def read_state(name: str, state_root: Path = STATE_ROOT) -> dict | None:
    path = cluster_state_path(name, state_root)
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise DevClusterError(f"cannot read state '{path}': {error}") from error


def write_state(name: str, state: dict, state_root: Path = STATE_ROOT) -> None:
    """Atomically replace ``state.json`` so readers never see a partial file."""
    directory = cluster_state_dir(name, state_root)
    directory.mkdir(parents=True, exist_ok=True)
    path = cluster_state_path(name, state_root)
    state = dict(state)
    state["updated_at"] = now()

    descriptor, temporary = tempfile.mkstemp(prefix=".state.", suffix=".json", dir=directory)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(state, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


def process_alive(pid: int | None) -> bool:
    if pid is None or pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        pass

    # A zombie has not been reaped yet but is not a running process.
    try:
        stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        state = stat.rsplit(")", 1)[1].split()[0]
        return state != "Z"
    except (OSError, IndexError):
        return True


def classify_state(state: dict | None) -> str:
    """Return missing, live, stale, or stopped for a retained state."""
    if state is None:
        return "missing"
    if state.get("stopped_at"):
        return "stopped"
    if any(process_alive(process.get("pid")) for process in state.get("processes", [])):
        return "live"
    return "stale"


@contextlib.contextmanager
def cluster_lock(name: str, state_root: Path = STATE_ROOT):
    """Serialize up/down for one cluster name."""
    directory = cluster_state_dir(name, state_root)
    directory.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(directory / ".lock", os.O_CREAT | os.O_RDWR, 0o644)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise DevClusterError(
                f"another dev_cluster command is operating on '{name}'"
            ) from error
        yield
    finally:
        with contextlib.suppress(OSError):
            fcntl.flock(descriptor, fcntl.LOCK_UN)
        os.close(descriptor)


def group_alive(pgid: int | None) -> bool:
    if pgid is None or pgid <= 0:
        return False
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


# ---------------------------------------------------------------------------
# Ports and process discovery
# ---------------------------------------------------------------------------


def connect_host(address: str) -> str:
    if address in ("0.0.0.0", "*"):
        return "127.0.0.1"
    if address == "::":
        return "::1"
    return address


def can_connect(address: str, port: int, timeout: float = 0.2) -> bool:
    host = connect_host(address)
    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    try:
        with socket.socket(family, socket.SOCK_STREAM) as sock:
            sock.settimeout(timeout)
            return sock.connect_ex((host, port)) == 0
    except OSError:
        return False


def _has_listening_socket(port: int) -> bool:
    """Look for a LISTEN socket on ``port`` (any address) in /proc."""
    for name in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            lines = Path(name).read_text(encoding="utf-8").splitlines()[1:]
        except OSError:
            continue
        for line in lines:
            fields = line.split()
            if len(fields) < 4:
                continue
            local, state = fields[1], fields[3]
            if state != "0A":
                continue
            try:
                local_port = int(local.rsplit(":", 1)[1], 16)
            except (IndexError, ValueError):
                continue
            if local_port == port:
                return True
    return False


def port_is_free(address: str, port: int) -> bool:
    host = address or "0.0.0.0"
    if _has_listening_socket(port) or can_connect(address, port):
        return False

    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    try:
        with socket.socket(family, socket.SOCK_STREAM) as sock:
            # The runtime listener uses SO_REUSEPORT. Matching that lets this
            # check tolerate TIME_WAIT left by a just-stopped cluster (the
            # /proc scan above already rejects a live connector).
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            sock.bind((host, port))
    except OSError:
        return False
    return True


def wait_for_port(address: str, port: int, timeout: float, process=None) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise DevClusterError(f"process exit state {process.returncode}")
        if can_connect(address, port):
            return
        time.sleep(0.05)
    raise DevClusterError(f"listener on {address}:{port} did not accept within {timeout:.0f}s")


def wait_for_gateway_ready(
    process_spec: ProcessSpec, task_type: str, process, timeout: float
) -> None:
    wait_for_port(process_spec.address, process_spec.port, timeout, process)
    if not task_type:
        return

    host = connect_host(process_spec.address)
    if ":" in host:
        host = f"[{host}]"
    url = f"http://{host}:{process_spec.port}/tasks/{task_type}"
    body = b"hello"
    deadline = time.monotonic() + timeout
    last_error = "no attempt made"
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise DevClusterError(f"process exit state {process.returncode}")
        request = urllib.request.Request(
            url,
            data=body,
            method="POST",
            headers={"content-type": "application/octet-stream"},
        )
        try:
            with urllib.request.urlopen(request, timeout=2.0) as response:
                response_body = response.read()
            if response_body == body:
                return
            last_error = f"readiness task returned {response_body[:120]!r}"
        except (urllib.error.URLError, ConnectionError, TimeoutError, OSError) as error:
            last_error = str(error)
        time.sleep(0.1)
    raise DevClusterError(f"readiness task '{task_type}' failed: {last_error}")


def binary_for(role: str, root: Path = REPO_ROOT) -> Path:
    relative = GATEWAY_BINARY if role == GATEWAY_ROLE else NODEAGENT_BINARY
    path = root / relative
    if not path.is_file():
        raise DevClusterError(f"{role} binary not found at {path}\n{BUILD_HINT}")
    return path


def display_path(path: Path, root: Path = REPO_ROOT) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


# ---------------------------------------------------------------------------
# Scaffold
# ---------------------------------------------------------------------------


def _scheduler_block(scheduler: str) -> list[str]:
    if scheduler == "probe":
        return [
            '      name: "probe"',
            "      typed_config:",
            '        "@type": "type.googleapis.com/strij.extensions.schedulers.probe.ProbeRoleSchedulerConfig"',
            "        candidate_count: 2",
            "        probe_deadline_ms: 1000",
        ]
    if scheduler == "capability_aware":
        return [
            '      name: "capability_aware"',
            "      typed_config:",
            '        "@type": "type.googleapis.com/strij.extensions.schedulers.capability_aware.CapabilityAwareSchedulerConfig"',
        ]
    if scheduler == "round_robin":
        return ['      name: "round_robin"']
    raise DevClusterError(
        f"unknown scheduler '{scheduler}' (expected one of: {', '.join(SCHEDULERS)})"
    )


def _handler_block(handler: str) -> list[str]:
    if handler == "echo":
        return [
            '  - name: "echo"',
            "    typed_config:",
            '      "@type": "type.googleapis.com/strij.extensions.task_handlers.echo.EchoTaskHandlerConfig"',
            "      capacity:",
            "        concurrency: 1024",
        ]
    if handler == "piped_executable":
        return [
            '  - name: "piped_executable"',
            "    typed_config:",
            '      "@type": "type.googleapis.com/strij.extensions.task_handlers.piped_executable.PipedExecutableTaskHandlerConfig"',
        ]
    if handler == "openworkflow":
        return [
            '  - name: "openworkflow"',
            "    typed_config:",
            '      "@type": "type.googleapis.com/strij.extensions.task_handlers.openworkflow.OpenWorkflowTaskHandlerConfig"',
            "      evaluator:",
            '        name: "jq"',
        ]
    raise DevClusterError(
        f"unknown task handler '{handler}' "
        f"(expected one of: {', '.join(DEFAULT_HANDLERS)})"
    )


def render_gateway_config(
    *,
    port: int,
    node_ports: list[int],
    scheduler: str,
    gateway_id: str,
    event_plane: bool,
) -> str:
    lines = [
        "# Generated by tools/dev_cluster.py; edit freely, then `up` the preset.",
        "http_listener:",
        '  address: "127.0.0.1"',
        f"  port: {port}",
        "",
        "node_discovery:",
        '  name: "static"',
        "  typed_config:",
        '    "@type": "type.googleapis.com/strij.config.StaticNodeDiscoveryConfig"',
        "    addresses:",
    ]
    for node_port in node_ports:
        lines.append(f'      - "127.0.0.1:{node_port}"')

    if event_plane:
        lines.extend(
            [
                "",
                "# Gateway-to-gateway event plane. Every gateway must know every node",
                "# (the complete-bipartite v1 invariant).",
                "event_plane:",
                "  extension:",
                '    name: "summary_mesh"',
                "    typed_config:",
                '      "@type": "type.googleapis.com/strij.extensions.event_planes.summary_mesh.SummaryMeshConfig"',
                "      refresh_interval_ms: 1000",
                "      peer_ttl_ms: 5000",
                f'  gateway_id: "{gateway_id}"',
            ]
        )

    lines.extend(
        [
            "",
            "schedulers:",
            "  # Empty task_type = the default scheduler for every task type.",
            "  - extension:",
            *_scheduler_block(scheduler),
            "",
            "logging:",
            '  level: "debug"',
            '  format: "text"',
            '  output: "stdout"',
            "  include_source_location: false",
            "",
        ]
    )
    return "\n".join(lines)


def render_node_config(*, port: int, handlers: tuple[str, ...]) -> str:
    lines = [
        "# Generated by tools/dev_cluster.py; edit freely, then `up` the preset.",
        "tlv_listener:",
        '  address: "127.0.0.1"',
        f"  port: {port}",
        "",
        "schedulers:",
        "  # Advertise both policies; the gateway picks one. `default` is the",
        "  # local authority for child tasks.",
        "  - extension:",
        '      name: "probe"',
        "      typed_config:",
        '        "@type": "type.googleapis.com/strij.extensions.schedulers.probe.ProbeSchedulerConfig"',
        "  - extension:",
        '      name: "push"',
        "  - extension:",
        '      name: "default"',
        "    local_default: true",
        "",
        "pools:",
        '  - name: "cpu"',
        "    total: 16",
        "",
        "task_handlers:",
    ]
    for handler in handlers:
        lines.extend(_handler_block(handler))
    lines.extend(
        [
            "",
            "heartbeat_interval:",
            "  seconds: 5",
            "",
            "logging:",
            '  level: "debug"',
            '  format: "text"',
            '  output: "stdout"',
            "  include_source_location: false",
            "",
        ]
    )
    return "\n".join(lines)


def render_manifest(
    *,
    name: str,
    nodes: int,
    gateways: int,
    readiness_task_type: str,
) -> str:
    lines = [
        "# Generated by tools/dev_cluster.py. The launcher reads ports from the",
        "# process configs; this manifest fixes the process set and boot order.",
        f'description: "Local development cluster \'{name}\'"',
        "",
        "readiness:",
        f'  task_type: "{readiness_task_type}"',
        "",
        "processes:",
    ]
    for index in range(nodes):
        lines.extend(
            [
                f'  - name: "node-{index}"',
                f'    role: "{NODE_ROLE}"',
                f'    config: "node-{index}.yaml"',
            ]
        )
    for index in range(gateways):
        lines.extend(
            [
                f'  - name: "gateway-{index}"',
                f'    role: "{GATEWAY_ROLE}"',
                f'    config: "gateway-{index}.yaml"',
            ]
        )
    lines.append("")
    return "\n".join(lines)


def render_preset(
    *,
    name: str,
    nodes: int,
    gateways: int,
    node_ports: list[int],
    gateway_ports: list[int],
    scheduler: str,
    event_plane: bool,
    handlers: tuple[str, ...],
    readiness_task_type: str = DEFAULT_READINESS_TASK_TYPE,
) -> dict[str, str]:
    if nodes < 1:
        raise DevClusterError("a preset needs at least one node (--nodes >= 1)")
    if gateways < 1:
        raise DevClusterError("a preset needs at least one gateway (--gateways >= 1)")
    if scheduler not in SCHEDULERS:
        raise DevClusterError(
            f"unknown scheduler '{scheduler}' (expected one of: {', '.join(SCHEDULERS)})"
        )
    if not handlers:
        raise DevClusterError("a preset needs at least one task handler (--handlers)")
    for handler in handlers:
        _handler_block(handler)
    if len(node_ports) != nodes or len(gateway_ports) != gateways:
        raise DevClusterError("internal error: port count does not match process count")

    all_ports = [*node_ports, *gateway_ports]
    for port in all_ports:
        if port < 1 or port > 65535:
            raise DevClusterError(f"port {port} is outside 1..65535")
    if len(set(all_ports)) != len(all_ports):
        raise DevClusterError(
            "requested node and gateway port ranges overlap; choose distinct ranges"
        )

    files = {
        "cluster.yaml": render_manifest(
            name=name,
            nodes=nodes,
            gateways=gateways,
            readiness_task_type=readiness_task_type,
        )
    }
    for index, port in enumerate(node_ports):
        files[f"node-{index}.yaml"] = render_node_config(port=port, handlers=handlers)
    for index, port in enumerate(gateway_ports):
        files[f"gateway-{index}.yaml"] = render_gateway_config(
            port=port,
            node_ports=node_ports,
            scheduler=scheduler,
            gateway_id=f"{name}-gateway-{index}",
            event_plane=event_plane,
        )
    return files


def _preset_ports(preset: Preset) -> dict[int, str]:
    return {process.port: process.name for process in preset.processes}


def find_port_conflict(
    presets_dir: Path, name: str, ports: list[int]
) -> tuple[int, str, str] | None:
    wanted = set(ports)
    for preset in discover_presets(presets_dir):
        if preset.name == name:
            continue
        for port, process_name in _preset_ports(preset).items():
            if port in wanted:
                return port, preset.name, process_name
    return None


def suggest_port_range(
    presets_dir: Path,
    *,
    count: int,
    start: int,
    also_used: set[int],
    name: str,
) -> int:
    used = set(also_used)
    for preset in discover_presets(presets_dir):
        if preset.name == name:
            continue
        used.update(_preset_ports(preset))
    candidate = start
    while any(candidate + offset in used for offset in range(count)):
        candidate += 1
    return candidate


def scaffold_preset(
    name: str,
    *,
    presets_dir: Path = PRESETS_DIR,
    nodes: int = DEFAULT_NODES,
    gateways: int = DEFAULT_GATEWAYS,
    node_port: int = DEFAULT_NODE_PORT,
    gateway_port: int = DEFAULT_GATEWAY_PORT,
    scheduler: str = DEFAULT_SCHEDULER,
    event_plane: bool = True,
    handlers: tuple[str, ...] = DEFAULT_HANDLERS,
    force: bool = False,
) -> Path:
    if not NAME_RE.match(name):
        raise DevClusterError(
            f"invalid preset name '{name}' (use letters, digits, '-' and '_')"
        )
    if nodes < 1:
        raise DevClusterError("a preset needs at least one node (--nodes >= 1)")
    if gateways < 1:
        raise DevClusterError("a preset needs at least one gateway (--gateways >= 1)")

    node_ports = [node_port + index for index in range(nodes)]
    gateway_ports = [gateway_port + index for index in range(gateways)]
    for port in [*node_ports, *gateway_ports]:
        if port < 1 or port > 65535:
            raise DevClusterError(f"port {port} is outside 1..65535")
    if len(set([*node_ports, *gateway_ports])) != len(node_ports) + len(gateway_ports):
        raise DevClusterError(
            "requested node and gateway port ranges overlap; choose distinct ranges"
        )

    directory = presets_dir / name
    if directory.exists() and not force:
        raise DevClusterError(
            f"preset '{name}' already exists at {directory}; pass --force to overwrite it"
        )

    conflict = find_port_conflict(presets_dir, name, [*node_ports, *gateway_ports])
    if conflict is not None:
        port, other_name, other_process = conflict
        suggested_node = suggest_port_range(
            presets_dir,
            count=nodes,
            start=node_port,
            also_used=set(gateway_ports),
            name=name,
        )
        suggested_gateway = suggest_port_range(
            presets_dir,
            count=gateways,
            start=gateway_port,
            also_used=set(range(suggested_node, suggested_node + nodes)),
            name=name,
        )
        raise DevClusterError(
            f"port {port} needed by this preset is already used by preset "
            f"'{other_name}' ({other_process}); try "
            f"--gateway-port {suggested_gateway} --node-port {suggested_node}"
        )

    files = render_preset(
        name=name,
        nodes=nodes,
        gateways=gateways,
        node_ports=node_ports,
        gateway_ports=gateway_ports,
        scheduler=scheduler,
        event_plane=event_plane,
        handlers=handlers,
    )

    if force and directory.exists():
        # Drop generated files from a previous topology so --force truly
        # rewrites the preset; leave unrelated files alone.
        stale_paths = [directory / "cluster.yaml"]
        stale_paths.extend(directory.glob("node-*.yaml"))
        stale_paths.extend(directory.glob("gateway-*.yaml"))
        for stale in stale_paths:
            with contextlib.suppress(FileNotFoundError):
                stale.unlink()

    directory.mkdir(parents=True, exist_ok=True)
    for filename, content in files.items():
        (directory / filename).write_text(content, encoding="utf-8")
    return directory


# ---------------------------------------------------------------------------
# Start and status
# ---------------------------------------------------------------------------


def tail_file(path: Path, lines: int = LOG_TAIL_LINES) -> str:
    try:
        content = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    return "\n".join(content[-lines:])


def spawn_process(spec: ProcessSpec, log_path: Path, root: Path = REPO_ROOT):
    executable = binary_for(spec.role, root)
    arguments = [str(executable), f"--config_file={spec.config}"]
    with open(log_path, "wb") as log_stream:
        process = subprocess.Popen(
            arguments,
            cwd=root,
            stdin=subprocess.DEVNULL,
            stdout=log_stream,
            stderr=subprocess.STDOUT,
            start_new_session=True,
            close_fds=True,
        )
    return process


def _process_state(
    spec: ProcessSpec, pid: int, log_path: Path, root: Path = REPO_ROOT
) -> dict:
    return {
        "name": spec.name,
        "role": spec.role,
        "pid": pid,
        "pgid": pid,
        "address": spec.address,
        "port": spec.port,
        "config": display_path(spec.config, root),
        "log": display_path(log_path, root),
        "started_at": now(),
    }


def _signal_group(process: dict, signal_number: int) -> None:
    pgid = process.get("pgid")
    if not group_alive(pgid):
        return
    try:
        os.killpg(pgid, signal_number)
    except ProcessLookupError:
        pass


def _wait_groups_gone(processes: list[dict], timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not any(group_alive(process.get("pgid")) for process in processes):
            return True
        time.sleep(0.05)
    return not any(group_alive(process.get("pgid")) for process in processes)


def _wait_port_closed(address: str, port: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not can_connect(address, port):
            return True
        time.sleep(0.05)
    return not can_connect(address, port)


def stop_cluster_processes(
    name: str,
    state: dict,
    *,
    state_root: Path = STATE_ROOT,
    grace: float = DEFAULT_GRACE_SECONDS,
    verify_ports: bool = True,
    mark_stopping: bool = True,
) -> tuple[list[str], list[ProcessSpec]]:
    """Gateway-first SIGTERM, bounded grace, SIGKILL stragglers, port check."""
    processes = state.get("processes", [])
    if mark_stopping and not state.get("stopping_at"):
        state["stopping_at"] = now()
        write_state(name, state, state_root)

    gateways = [process for process in processes if process.get("role") == GATEWAY_ROLE]
    nodes = [process for process in processes if process.get("role") == NODE_ROLE]
    escalated: list[str] = []

    for group in (gateways, nodes):
        for process in group:
            _signal_group(process, signal.SIGTERM)
        if not _wait_groups_gone(group, grace):
            stragglers = [
                process for process in group if group_alive(process.get("pgid"))
            ]
            for process in stragglers:
                _signal_group(process, signal.SIGKILL)
                escalated.append(process["name"])
            _wait_groups_gone(stragglers, 2.0)

    busy: list[ProcessSpec] = []
    if verify_ports:
        for process in processes:
            if not _wait_port_closed(
                process.get("address", "127.0.0.1"),
                process.get("port", 0),
                PORT_CHECK_TIMEOUT_SECONDS,
            ):
                busy.append(
                    ProcessSpec(
                        name=process["name"],
                        role=process.get("role", NODE_ROLE),
                        config=Path(process.get("config", "")),
                        address=process.get("address", "127.0.0.1"),
                        port=process.get("port", 0),
                    )
                )

    state["stopped_at"] = now()
    write_state(name, state, state_root)
    return escalated, busy


def up_cluster(
    name: str,
    *,
    presets_dir: Path = PRESETS_DIR,
    state_root: Path = STATE_ROOT,
    root: Path = REPO_ROOT,
    timeout: float = READINESS_TIMEOUT_SECONDS,
    grace: float = DEFAULT_GRACE_SECONDS,
) -> dict:
    preset = load_preset(name, presets_dir)

    # Fail before writing any state or spawning anything.
    for process in preset.processes:
        binary_for(process.role, root)

    with cluster_lock(name, state_root):
        existing = read_state(name, state_root)
        classification = classify_state(existing)
        if classification == "live":
            live = next(
                process["name"]
                for process in existing["processes"]
                if process_alive(process.get("pid"))
            )
            raise DevClusterError(
                f"cluster '{name}' is already running ({live} is alive); "
                f"run `down {name}` first"
            )

        for process in preset.processes:
            if not port_is_free(process.address, process.port):
                raise DevClusterError(
                    f"port {process.port} needed by process '{process.name}' "
                    f"({process.address}) is already in use"
                )

        directory = cluster_state_dir(name, state_root)
        directory.mkdir(parents=True, exist_ok=True)
        log_paths = {
            process.name: directory / f"{process.name}.log"
            for process in preset.processes
        }
        # Logs start fresh: truncate before the new process can write.
        for log_path in log_paths.values():
            with open(log_path, "wb"):
                pass

        state = {
            "name": name,
            "preset_dir": display_path(preset.directory, root),
            "created_at": now(),
            "processes": [],
        }
        write_state(name, state, state_root)

        running = {}
        current: ProcessSpec | None = None
        try:
            nodes = [
                process
                for process in preset.processes
                if process.role == NODE_ROLE
            ]
            gateways = [
                process
                for process in preset.processes
                if process.role == GATEWAY_ROLE
            ]

            for process in nodes:
                current = process
                running[process.name] = spawn_process(
                    process, log_paths[process.name], root
                )
                state["processes"].append(
                    _process_state(
                        process, running[process.name].pid, log_paths[process.name], root
                    )
                )
                write_state(name, state, state_root)

            # Nodes-first barrier: no gateway dials before every TLV listener
            # accepts.
            for process in nodes:
                current = process
                wait_for_port(
                    process.address,
                    process.port,
                    timeout,
                    running[process.name],
                )

            # Gateways one at a time, each proven ready through its HTTP path.
            for process in gateways:
                current = process
                running[process.name] = spawn_process(
                    process, log_paths[process.name], root
                )
                state["processes"].append(
                    _process_state(
                        process, running[process.name].pid, log_paths[process.name], root
                    )
                )
                write_state(name, state, state_root)
                wait_for_gateway_ready(
                    process,
                    preset.readiness_task_type,
                    running[process.name],
                    timeout,
                )
        except (DevClusterError, OSError) as error:
            failed_name = current.name if current is not None else "(unknown)"
            failed_log = log_paths.get(failed_name)
            tail = tail_file(failed_log) if failed_log is not None else ""
            if not tail and failed_log is not None:
                tail = "(log is empty)"
            stop_cluster_processes(
                name,
                state,
                state_root=state_root,
                grace=min(grace, 2.0),
                verify_ports=False,
                mark_stopping=False,
            )
            message = f"cluster '{name}' failed to start: {error}"
            if failed_log is not None:
                message += (
                    f"\n--- process '{failed_name}' log tail ({display_path(failed_log, root)}) ---\n"
                    f"{tail}"
                )
            raise DevClusterError(message) from error

        print(f"Cluster '{name}' is up.")
        for process in state["processes"]:
            print(
                f"  {process['name']:<12} {process['role']:<8} "
                f"pid {process['pid']:<8} port {process['port']:<6} "
                f"{process['log']}"
            )
        readiness_task = preset.readiness_task_type or DEFAULT_READINESS_TASK_TYPE
        gateways = [
            process
            for process in preset.processes
            if process.role == GATEWAY_ROLE
        ]
        for index, process in enumerate(gateways):
            host = connect_host(process.address)
            if ":" in host:
                host = f"[{host}]"
            label = "Try" if index == 0 else "    "
            print(
                f"{label}: curl -d hello http://{host}:{process.port}/tasks/{readiness_task}"
            )
        return state


def known_cluster_names(state_root: Path = STATE_ROOT) -> list[str]:
    if not state_root.is_dir():
        return []
    return sorted(
        directory.name
        for directory in state_root.iterdir()
        if (directory / "state.json").is_file()
    )


def _print_cluster_status(name: str, state: dict) -> None:
    classification = classify_state(state)
    print(f"Cluster '{name}'  ({classification})")
    processes = state.get("processes", [])
    if not processes:
        print("  (no processes recorded)")
        return
    for process in processes:
        alive = process_alive(process.get("pid"))
        status = "running" if alive else "stopped"
        print(
            f"  {process['name']:<12} {process.get('role', '?'):<8} "
            f"pid {str(process.get('pid', '-')):<8} "
            f"port {str(process.get('port', '-')):<6} "
            f"{status:<8} {process.get('log', '-')}"
        )


def cmd_status(args: argparse.Namespace) -> int:
    if args.name:
        state = read_state(args.name)
        if state is None:
            known = ", ".join(known_cluster_names()) or "(none)"
            raise DevClusterError(
                f"no retained state for cluster '{args.name}'; known clusters: {known}"
            )
        _print_cluster_status(args.name, state)
        return 0

    names = known_cluster_names()
    if not names:
        print("No dev clusters have been started.")
        return 0
    for name in names:
        state = read_state(name)
        if state is not None:
            _print_cluster_status(name, state)
    return 0


def cmd_down(args: argparse.Namespace) -> int:
    name = args.name
    with cluster_lock(name):
        state = read_state(name)
        if state is None:
            print(f"Nothing is running for cluster '{name}' (no retained state).")
            return 0

        classification = classify_state(state)
        if classification == "stopped":
            print(f"Cluster '{name}' is already stopped.")
            return 0

        had_live_groups = any(
            group_alive(process.get("pgid")) for process in state.get("processes", [])
        )
        escalated, busy = stop_cluster_processes(
            name,
            state,
            grace=args.grace,
            verify_ports=had_live_groups,
        )

        print(f"Stopped cluster '{name}'.")
        if escalated:
            print(f"  escalated (SIGKILL after {args.grace:.1f}s): {', '.join(escalated)}")
        for process in state.get("processes", []):
            print(
                f"  {process['name']:<12} pid {str(process.get('pid', '-')):<8} "
                f"stopped  {process.get('log', '-')}"
            )

        if busy:
            names = ", ".join(
                f"{process.name} ({process.address}:{process.port})" for process in busy
            )
            raise DevClusterError(
                f"processes stopped but these former listener ports still accept "
                f"connections: {names}"
            )
    return 0


def _follow_reader(
    process_name: str,
    path: Path,
    start_offset: int,
    queue,
    stop_event: threading.Event,
) -> None:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as stream:
            # Resume exactly where the initial tail stopped, so lines appended
            # while the tail was read and printed are not dropped.
            stream.seek(start_offset)
            while not stop_event.is_set():
                line = stream.readline()
                if line:
                    queue.put((process_name, line.rstrip("\n")))
                else:
                    time.sleep(0.05)
    except OSError:
        return


def _tail_lines(path: Path, count: int) -> tuple[list[str] | None, int]:
    """Return the last ``count`` lines and the offset where they end.

    The offset is the file size observed before reading, so a follower can
    resume exactly where the tail stopped without dropping appended lines.
    A missing or unreadable file returns ``(None, 0)``.
    """
    try:
        with open(path, "rb") as stream:
            stream.seek(0, os.SEEK_END)
            end = stream.tell()
            stream.seek(0)
            data = stream.read(end)
    except OSError:
        return None, 0
    lines = data.decode("utf-8", errors="replace").splitlines()
    if count <= 0:
        return [], end
    return lines[-count:], end


def cmd_logs(args: argparse.Namespace) -> int:
    state = read_state(args.name)
    if state is None:
        known = ", ".join(known_cluster_names()) or "(none)"
        raise DevClusterError(
            f"no retained state for cluster '{args.name}'; known clusters: {known}"
        )

    processes = state.get("processes", [])
    if args.process is not None:
        selected = [p for p in processes if p["name"] == args.process]
        if not selected:
            known = ", ".join(process["name"] for process in processes) or "(none)"
            raise DevClusterError(
                f"cluster '{args.name}' has no process '{args.process}'; "
                f"known processes: {known}"
            )
    else:
        selected = processes

    start_offsets: dict[str, int] = {}
    for process in selected:
        path = Path(process.get("log", ""))
        if not path.is_absolute():
            path = REPO_ROOT / path
        lines, start_offset = _tail_lines(path, args.lines)
        start_offsets[process["name"]] = start_offset
        if lines is None:
            print(
                f"*** {process['name']}: no log file at {process.get('log', '-')}",
                file=sys.stderr,
            )
            continue
        if not lines:
            print(f"*** {process['name']}: log file is empty", file=sys.stderr)
            continue
        for line in lines:
            if args.no_prefix:
                print(line)
            else:
                print(f"[{process['name']}] {line}")

    if not args.follow:
        return 0

    stop_event = threading.Event()
    queue_ = queue.Queue()
    for process in selected:
        path = Path(process.get("log", ""))
        if not path.is_absolute():
            path = REPO_ROOT / path
        if not path.is_file():
            continue
        thread = threading.Thread(
            target=_follow_reader,
            args=(
                process["name"],
                path,
                start_offsets.get(process["name"], 0),
                queue_,
                stop_event,
            ),
            daemon=True,
        )
        thread.start()

    alive = {
        process["name"]: process_alive(process.get("pid")) for process in selected
    }
    exiting_reported: set[str] = set()
    last_poll = 0.0
    print(f"--- following {args.name} (Ctrl-C to stop) ---", file=sys.stderr)

    try:
        while True:
            try:
                process_name, line = queue_.get(timeout=0.1)
            except queue.Empty:
                pass
            else:
                if args.no_prefix:
                    print(line, flush=True)
                else:
                    print(f"[{process_name}] {line}", flush=True)

            current = time.monotonic()
            if current - last_poll < 0.5:
                continue
            last_poll = current
            latest = read_state(args.name) or state
            stopping = bool(latest.get("stopping_at"))
            for process in selected:
                process_name = process["name"]
                if process_name in exiting_reported:
                    continue
                was_alive = alive.get(process_name, False)
                is_alive = process_alive(process.get("pid"))
                if was_alive and not is_alive:
                    exiting_reported.add(process_name)
                    pid = process.get("pid", "-")
                    if stopping:
                        print(
                            f"*** {process_name} (pid {pid}) stopped", flush=True
                        )
                    else:
                        print(
                            f"*** {process_name} (pid {pid}) exited unexpectedly; "
                            f"cluster is degraded",
                            flush=True,
                        )
                alive[process_name] = is_alive

            if stopping and not any(alive.values()):
                break
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
    return 0


def cmd_clean(args: argparse.Namespace) -> int:
    directory = cluster_state_dir(args.name)
    if not directory.exists():
        print(f"Nothing to clean for cluster '{args.name}'.")
        return 0

    with cluster_lock(args.name):
        state = read_state(args.name)
        if classify_state(state) == "live":
            raise DevClusterError(
                f"cluster '{args.name}' is running; run `down {args.name}` first"
            )
        shutil.rmtree(directory)
    print(f"Removed runtime state for cluster '{args.name}' ({display_path(directory)}).")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def cmd_list(args: argparse.Namespace) -> int:
    presets = discover_presets()
    if args.json:
        payload = [
            {
                "name": preset.name,
                "directory": str(preset.directory),
                "description": preset.description,
                "readiness_task_type": preset.readiness_task_type,
                "processes": [
                    {
                        "name": process.name,
                        "role": process.role,
                        "address": process.address,
                        "port": process.port,
                        "config": str(process.config),
                    }
                    for process in preset.processes
                ],
            }
            for preset in presets
        ]
        print(json.dumps(payload, indent=2))
        return 0

    if not presets:
        print(f"No presets under {PRESETS_DIR}")
        return 0

    for preset in presets:
        print(f"{preset.name}  ({preset.directory})")
        if preset.description:
            print(f"  {preset.description}")
        for process in preset.processes:
            print(
                f"  {process.name:<12} {process.role:<8} "
                f"{process.address}:{process.port:<6} {display_path(process.config)}"
            )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Manage local multi-process Strij development clusters."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="list available presets")
    list_parser.add_argument("--json", action="store_true", help="print JSON")
    list_parser.set_defaults(handler=cmd_list)

    scaffold_parser = subparsers.add_parser(
        "scaffold", help="generate a preset (one-time; refuse to overwrite)"
    )
    scaffold_parser.add_argument("name")
    scaffold_parser.add_argument("--nodes", type=int, default=DEFAULT_NODES)
    scaffold_parser.add_argument("--gateways", type=int, default=DEFAULT_GATEWAYS)
    scaffold_parser.add_argument("--node-port", type=int, default=DEFAULT_NODE_PORT)
    scaffold_parser.add_argument(
        "--gateway-port", type=int, default=DEFAULT_GATEWAY_PORT
    )
    scaffold_parser.add_argument(
        "--scheduler", choices=SCHEDULERS, default=DEFAULT_SCHEDULER
    )
    scaffold_parser.add_argument(
        "--no-event-plane",
        action="store_true",
        help="omit the summary_mesh event plane",
    )
    scaffold_parser.add_argument(
        "--handlers",
        default=",".join(DEFAULT_HANDLERS),
        help="comma-separated task handler list",
    )
    scaffold_parser.add_argument(
        "--force", action="store_true", help="overwrite an existing preset"
    )
    scaffold_parser.set_defaults(handler=cmd_scaffold)

    up_parser = subparsers.add_parser("up", help="start a preset cluster")
    up_parser.add_argument("name")
    up_parser.add_argument(
        "--timeout",
        type=float,
        default=READINESS_TIMEOUT_SECONDS,
        help="per-process readiness timeout in seconds",
    )
    up_parser.set_defaults(handler=cmd_up)

    status_parser = subparsers.add_parser(
        "status", help="show process identity for one or all clusters"
    )
    status_parser.add_argument("name", nargs="?")
    status_parser.set_defaults(handler=cmd_status)

    down_parser = subparsers.add_parser("down", help="stop a cluster")
    down_parser.add_argument("name")
    down_parser.add_argument(
        "--grace",
        type=float,
        default=DEFAULT_GRACE_SECONDS,
        help="seconds to wait after SIGTERM before SIGKILL",
    )
    down_parser.set_defaults(handler=cmd_down)

    logs_parser = subparsers.add_parser("logs", help="show merged process logs")
    logs_parser.add_argument("name")
    logs_parser.add_argument(
        "process", nargs="?", help="restrict output to one process"
    )
    logs_parser.add_argument(
        "-n", "--lines", type=int, default=100, help="tail length per process"
    )
    logs_parser.add_argument(
        "-f", "--follow", action="store_true", help="follow appended lines"
    )
    logs_parser.add_argument(
        "--no-prefix", action="store_true", help="print raw log lines"
    )
    logs_parser.set_defaults(handler=cmd_logs)

    clean_parser = subparsers.add_parser(
        "clean", help="remove a cluster's runtime state and logs"
    )
    clean_parser.add_argument("name")
    clean_parser.set_defaults(handler=cmd_clean)

    return parser


def cmd_up(args: argparse.Namespace) -> int:
    up_cluster(args.name, timeout=args.timeout)
    return 0


def cmd_scaffold(args: argparse.Namespace) -> int:
    handlers = tuple(
        handler.strip() for handler in args.handlers.split(",") if handler.strip()
    )
    directory = scaffold_preset(
        args.name,
        nodes=args.nodes,
        gateways=args.gateways,
        node_port=args.node_port,
        gateway_port=args.gateway_port,
        scheduler=args.scheduler,
        event_plane=not args.no_event_plane,
        handlers=handlers,
        force=args.force,
    )
    print(f"Wrote preset '{args.name}' to {directory}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except DevClusterError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
