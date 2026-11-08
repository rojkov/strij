# Spec Delta

## Purpose

Local development cluster presets and lifecycle: scaffold a repeatable
multi-process topology, start and stop it on one machine, inspect process
state, and read merged logs while investigating issues.

## ADDED Requirements

### Requirement: Preset layout

A cluster preset SHALL be a directory under `config/examples/<name>/` holding a
`cluster.yaml` manifest plus one standard gateway or node agent YAML per
process. The manifest SHALL list each process's name, role, and config file, and
MAY declare the readiness task type. Each process's listener port SHALL be read
from its own config; the manifest SHALL NOT repeat it.

#### Scenario: Manifest defines the processes

- **WHEN** a preset manifest lists two nodes and one gateway
- **THEN** `up` starts exactly those processes with those roles
- **AND** the boot order follows the manifest (all nodes, then gateways)

#### Scenario: Ports have a single source of truth

- **WHEN** a process config declares a listener port
- **THEN** the launcher uses that port for spawning and readiness probing
- **AND** no port is required in the manifest

#### Scenario: Invalid preset entries fail before spawning

- **WHEN** a manifest names a role other than node or gateway, or references a
  config file that does not exist
- **THEN** the command fails before any process is spawned
- **AND** the error names the offending entry

### Requirement: Presets are read-only to the launcher

`up`, `down`, `status`, and `logs` SHALL NOT create, modify, or delete files
under `config/examples/`. A preset is authored once, or generated once by
`scaffold`, and committed to the repository.

#### Scenario: Running does not touch the preset

- **WHEN** a cluster is started, inspected, and stopped
- **THEN** the preset directory is unchanged

#### Scenario: Missing preset points at scaffold

- **WHEN** `up` names a preset that does not exist
- **THEN** it fails naming the available presets and the scaffold command

### Requirement: One-time scaffold

`scaffold <name>` SHALL generate a preset for the requested topology and SHALL
refuse to overwrite an existing preset unless `--force` is given. It SHALL write
the manifest and one config per process, and every generated gateway's static
node discovery SHALL list every generated node.

#### Scenario: Default topology

- **WHEN** `scaffold local` runs with defaults
- **THEN** it writes `config/examples/local/` with two node configs, one gateway
  config, and a manifest listing them in boot order

#### Scenario: Existing preset is protected

- **WHEN** the preset directory already exists and `--force` is absent
- **THEN** scaffold fails without writing anything
- **AND** with `--force` it rewrites the preset

#### Scenario: Every gateway discovers every node

- **WHEN** `scaffold big --nodes 3 --gateways 2` runs
- **THEN** each gateway config lists all three node addresses
- **AND** the manifest lists all five processes

#### Scenario: Empty topology is rejected

- **WHEN** `--nodes 0` or `--gateways 0` is requested
- **THEN** scaffold fails without writing a preset

### Requirement: Scaffold defaults match the development setup

Generated presets SHALL default to `probe` gateway scheduling
(`candidate_count` 2, `probe_deadline_ms` 1000), nodes that advertise `probe`
and `push` while keeping `default` as the local authority, logging level
`debug`, format `text`, output `stdout`, and a `summary_mesh` event plane with a
unique `gateway_id` per gateway. `--scheduler` SHALL accept `probe`,
`capability_aware`, or `round_robin`.

#### Scenario: Default preset uses probe and debug

- **WHEN** `scaffold local` runs with defaults
- **THEN** the gateway config binds the `probe` scheduler
- **AND** every process config sets `logging.level` to `debug`

#### Scenario: Nodes advertise both policy protocols

- **WHEN** a node config is generated
- **THEN** it includes a `probe` scheduler and a `push` scheduler
- **AND** it declares `default` with `local_default: true`

#### Scenario: Scheduler policy can be overridden

- **WHEN** `scaffold alt --scheduler capability_aware` runs
- **THEN** the generated gateway binds `capability_aware`
- **AND** the node configs still advertise `probe` and `push`

#### Scenario: Event-plane gateways get unique ids

- **WHEN** a preset with an event plane generates more than one gateway
- **THEN** every gateway has a distinct `gateway_id`

#### Scenario: Event plane can be omitted

- **WHEN** `scaffold plain --no-event-plane` runs
- **THEN** the gateway configs contain no `event_plane` section

### Requirement: Preset ports are fixed and unique

Each preset SHALL own a fixed port range. `scaffold` SHALL reject a requested
range that another preset already uses, naming the conflict and an available
alternative. `up` SHALL verify every required port is free before spawning and
SHALL fail naming the bound port.

#### Scenario: Scaffold rejects a colliding range

- **WHEN** the requested gateway or node port is already used by another preset
- **THEN** scaffold fails without writing a preset
- **AND** it names the conflicting preset and suggests a free range

#### Scenario: Start rejects a busy port

- **WHEN** a required port is already bound on the machine
- **THEN** `up` fails before spawning any process
- **AND** the error names the port and the process that needed it

### Requirement: Start order and readiness

`up` SHALL start every node and wait until each TLV listener accepts a
connection before starting any gateway, because a gateway never re-dials a
refused address. It SHALL then start gateways one at a time and SHALL consider a
gateway ready only after the manifest's readiness task completes through its
HTTP listener.

#### Scenario: Nodes listen before any gateway dials

- **WHEN** a cluster with N nodes and M gateways starts
- **THEN** all N node listeners accept before the first gateway is spawned
- **AND** subsequent gateways are spawned only after the previous one is ready

#### Scenario: Readiness goes through the request path

- **WHEN** a gateway process is spawned
- **THEN** readiness requires a successful readiness task submission through
  that gateway's HTTP listener, not just an open port

#### Scenario: Boot failure is diagnosable

- **WHEN** a process exits during boot
- **THEN** `up` stops the processes it already started
- **AND** it prints that process's name, exit state, and log tail
- **AND** it exits non-zero

### Requirement: Persisted state and cluster guard

`up` SHALL record each process in `.devcluster/<name>/state.json` as it starts,
including pid, process group, port, config path, and log path. It SHALL refuse to
start when retained state lists a live process, and SHALL truncate that cluster's
log files.

#### Scenario: State survives the start command

- **WHEN** `up` returns successfully
- **THEN** `.devcluster/<name>/state.json` lists every running process with its
  pid and port
- **AND** the processes remain running after the command exits

#### Scenario: A live cluster cannot be started twice

- **WHEN** retained state for the same name lists a live pid
- **THEN** `up` refuses to start and names the running cluster

#### Scenario: Stale state is replaced

- **WHEN** retained state exists but none of its pids are alive
- **THEN** `up` starts normally and overwrites the state

#### Scenario: Logs start fresh

- **WHEN** `up` starts a cluster that has logs from an earlier run
- **THEN** each process log is truncated before the new process writes to it

### Requirement: Graceful stop and escalation

`down` SHALL signal the process groups, wait a bounded grace period, force-kill
any remaining group, verify that former listener ports are closed, retain the
logs, and mark the retained state stopped. `down` against a cluster with no
retained state SHALL report that nothing is running and exit successfully.

#### Scenario: Graceful shutdown

- **WHEN** a running cluster is stopped
- **THEN** every process receives a termination signal and exits successfully
- **AND** no process requires a forced kill

#### Scenario: A wedged process is escalated

- **WHEN** a process does not exit within the grace period
- **THEN** its process group is force-killed
- **AND** `down` reports that escalation occurred

#### Scenario: Task descendants do not survive

- **WHEN** a node has spawned child processes while running tasks
- **THEN** stopping the cluster terminates those descendants through the process
  group

#### Scenario: Ports are released

- **WHEN** a cluster is stopped
- **THEN** every former listener port refuses new connections

#### Scenario: Stopped state is retained for postmortem

- **WHEN** a cluster is stopped
- **THEN** its logs remain under `.devcluster/<name>/`
- **AND** `state.json` still records the last pids, marked stopped

#### Scenario: Stopping nothing is not an error

- **WHEN** `down` names a cluster with no retained state
- **THEN** it reports that nothing is running and exits successfully

### Requirement: Status exposes process identity

`status` SHALL list every known cluster's processes with name, role, pid, port,
running or stopped state, and log path, and SHALL work from retained state after
`down`. The reported pid SHALL be the debugger attach target and the correlation
key for core dumps.

#### Scenario: Running cluster shows live pids

- **WHEN** `status` inspects a running cluster
- **THEN** every process is reported with its live pid and port
- **AND** the output is sufficient to attach a debugger or locate its log

#### Scenario: Stopped cluster still reports pids

- **WHEN** `status` inspects a cluster that was stopped
- **THEN** the processes are reported as stopped with their retained pids

#### Scenario: Unknown cluster is named

- **WHEN** `status <name>` names a cluster with no state
- **THEN** it fails listing the known clusters

### Requirement: Merged log viewing

`logs <name>` SHALL merge the last N lines of each process log, each prefixed
with the process name; `-f` SHALL follow appended lines; a process argument
SHALL restrict output to that process; `--no-prefix` SHALL print raw lines. A
missing or empty log file SHALL be reported without failing the command.

#### Scenario: Merged tail carries the process name

- **WHEN** `logs local` prints a cluster with two nodes and a gateway
- **THEN** every line is prefixed with the process name it came from

#### Scenario: Follow mode

- **WHEN** `logs local -f` is running and a process appends a line
- **THEN** that line appears in the output while the command keeps running

#### Scenario: Single-process view

- **WHEN** `logs local node-1` is requested
- **THEN** only `node-1`'s log is printed

#### Scenario: Raw view

- **WHEN** `--no-prefix` is passed
- **THEN** lines are printed exactly as written to the log file

#### Scenario: Missing log is not fatal

- **WHEN** a process has no log file, or its log is empty
- **THEN** the command reports that and still prints the other logs

### Requirement: Follow-mode exit visibility

While following, `logs -f` SHALL poll the pids recorded in state and print a
marker line when a process exits, distinguishing an expected stop from an
unexpected exit. An exit observed while the cluster is not stopping SHALL be
reported as unexpected and as degrading the cluster. Markers SHALL appear in
the viewer output only, never in the process log files.

#### Scenario: Unexpected exit is marked

- **WHEN** a gateway or node process dies while `logs -f` is attached and the
  cluster is not stopping
- **THEN** a marker naming the process and its pid reports an unexpected exit
- **AND** the marker is interleaved in the merged stream where the exit was
  observed

#### Scenario: Expected stop is marked

- **WHEN** `down` stops the cluster while `logs -f` is attached
- **THEN** each process exit is reported as stopped, not as an unexpected exit

#### Scenario: Markers do not modify logs

- **WHEN** the viewer prints an exit marker
- **THEN** the process log files are unchanged by the marker

#### Scenario: Exit without a viewer is still observable

- **WHEN** a process exits and no `-f` viewer is attached
- **THEN** `status` still reports that process as stopped

### Requirement: Ignored runtime footprint and cleanup

`.devcluster/` SHALL be ignored by git so runtime state and logs never appear as
repository changes. `clean <name>` SHALL remove that cluster's state and logs
while leaving presets untouched.

#### Scenario: Git never sees runtime files

- **WHEN** a cluster has run and written logs and state
- **THEN** `git status` reports no changes under `.devcluster/`

#### Scenario: Clean removes runtime files

- **WHEN** `clean local` runs
- **THEN** `.devcluster/local/` is gone
- **AND** `config/examples/local/` is unchanged

#### Scenario: Cleaning an unknown cluster is a no-op

- **WHEN** `clean` names a cluster with no runtime directory
- **THEN** it reports nothing to clean and exits successfully

### Requirement: Makefile entry points

The repository SHALL expose `make dev_scaffold`, `dev_up`, `dev_down`,
`dev_status`, `dev_logs`, and `dev_clean`. `dev_up` SHALL build the gateway and
node agent binaries before starting the cluster; the tool invoked directly
SHALL fail with a build hint when a binary is missing.

#### Scenario: Start builds first

- **WHEN** `make dev_up` runs on a clean tree
- **THEN** it builds `//src/gateway/exe:gateway` and
  `//src/nodeagent/exe:nodeagent` before starting the cluster

#### Scenario: Direct invocation without binaries

- **WHEN** the tool is invoked directly and a binary is missing
- **THEN** it fails with a message pointing at the build target

#### Scenario: Cluster selection is forwarded

- **WHEN** a developer passes `CLUSTER` or `ARGS` to a `make dev_*` target
- **THEN** the value is forwarded to the tool unchanged
