# Spec Delta

## Purpose

Automated end-to-end tests that start real gateway and nodeagent processes in isolated clusters,
prove readiness through the request path, and tear the cluster down without leaving processes or
ports behind.

## ADDED Requirements

### Requirement: Two-phase cluster boot

The harness SHALL start every node and wait until each node's TLV listener accepts a connection
before starting any gateway, because a gateway that fails its first connection attempt never
re-dials that node.

#### Scenario: Nodes are listening before gateways dial

- **GIVEN** a cluster specification with N nodes and M gateways
- **WHEN** the cluster is started
- **THEN** all N node listeners accept connections before the first gateway process is spawned
- **AND** no gateway reports a failed connection to a node address

#### Scenario: A gateway started before a node stays permanently blind

- **GIVEN** a node whose listener is not yet bound
- **WHEN** a gateway dials that node and the connection is refused
- **THEN** the gateway never re-dials that address for the rest of the process lifetime
- **AND** tasks submitted to that gateway report `CAPACITY_REFUSED` rather than waiting
- **AND** the harness documents this as the reason its boot order is fixed

### Requirement: Readiness is proven through the request path

The harness SHALL consider a cluster ready only after every gateway has served at least one task
from every node, identified by a per-node marker visible in the task result.

#### Scenario: Readiness requires complete fan-out

- **GIVEN** a started cluster
- **WHEN** the readiness barrier runs
- **THEN** it submits marker-bearing tasks through each gateway until every node marker has been
  observed at least once at that gateway, or a bounded deadline expires
- **AND** it reports readiness only when every gateway has observed every node

#### Scenario: Incomplete fan-out fails the test

- **GIVEN** a started cluster in which one node never accepts a gateway connection
- **WHEN** the readiness barrier expires
- **THEN** the test fails naming the gateway and the missing node marker
- **AND** the failure output includes the process logs needed to diagnose it

### Requirement: Per-test cluster isolation

Each test SHALL own its cluster, including its own allocated ports, temporary directory, and
processes, so tests neither collide nor depend on execution order.

#### Scenario: Sequential tests reuse no state

- **GIVEN** two tests that each start a cluster
- **WHEN** they run one after another in the same binary
- **THEN** each test uses freshly allocated ports and a fresh temporary directory
- **AND** nothing from the first test is visible to the second

#### Scenario: Parallel test binaries do not collide

- **GIVEN** two test binaries running concurrently
- **WHEN** each allocates listener ports
- **THEN** the ports are obtained from the operating system rather than from a fixed range
- **AND** neither binary binds a port held by the other

### Requirement: Generated configuration matches the topology

The harness SHALL generate every process configuration from a declarative cluster specification,
so a test expresses nodes, gateways, pools, and handlers rather than raw YAML.

#### Scenario: Every gateway knows every node

- **GIVEN** a specification with N nodes and M gateways
- **WHEN** the harness writes the gateway configurations
- **THEN** each gateway's static node discovery lists all N node addresses
- **AND** the resulting topology is complete bipartite

#### Scenario: Unsupported node configuration fails before requests run

- **GIVEN** a node specification missing a required element such as a resource pool
- **WHEN** the node process exits during startup
- **THEN** the harness detects the early exit during the boot barrier
- **AND** the test fails with the node's startup error instead of timing out

### Requirement: Requests are issued as black-box HTTP calls

The harness SHALL submit tasks exclusively through the gateway HTTP listener, returning the status
code and response body, and SHALL NOT reach into process internals to assert behavior.

#### Scenario: Fan-out returns each response once

- **GIVEN** a ready cluster and a set of distinct request bodies
- **WHEN** the requests are submitted concurrently through every gateway
- **THEN** every request reports a success status
- **AND** each response body matches its own request, with no cross-talk between connections

#### Scenario: Per-node capacity is observable across nodes

- **GIVEN** nodes whose shared pool admits one unit each, and requests requiring one unit
- **WHEN** more concurrent requests than total cluster capacity are submitted
- **THEN** exactly the number of requests that fit the per-node capacity succeed
- **AND** the remainder report `CAPACITY_REFUSED`

### Requirement: Deterministic teardown

The harness SHALL terminate each process with a graceful signal, escalate to a forced kill of the
process group after a bounded wait, reap every process, and verify that nothing survives.

#### Scenario: Graceful shutdown exits cleanly

- **GIVEN** a ready cluster with idle connections
- **WHEN** the test completes and the cluster is stopped
- **THEN** every process receives a termination signal and exits with status zero
- **AND** every process is reaped within the bounded wait

#### Scenario: A wedged process is escalated

- **GIVEN** a process that does not exit after the graceful signal
- **WHEN** the bounded wait expires
- **THEN** the harness sends a forced kill to the process group
- **AND** still reaps the process and reports that escalation occurred

#### Scenario: Task descendants do not survive

- **GIVEN** a node executing a task that has spawned child processes
- **WHEN** the cluster is stopped
- **THEN** no descendant of any cluster process remains running

#### Scenario: Listener ports are released

- **GIVEN** a stopped cluster
- **WHEN** the harness verifies teardown
- **THEN** connecting to each former listener port is refused

### Requirement: Failure output is self-contained

The harness SHALL capture each process's standard output and standard error to a file and include
the tail of those files, together with the process exit status, in any test failure.

#### Scenario: Boot failure prints the failing process log

- **GIVEN** a node that exits during startup because its configuration is invalid
- **WHEN** the boot barrier observes the exit
- **THEN** the test output contains the process name, its exit status, and the tail of its log

#### Scenario: Readiness timeout prints every log

- **GIVEN** a cluster that never reaches readiness
- **WHEN** the readiness deadline expires
- **THEN** the test output contains the tail of every node and gateway log

### Requirement: Unsupported environments skip instead of failing

The harness SHALL probe the event-loop facility it depends on and skip the suite with an
explanatory message when the facility is unavailable, because the binaries cannot start there.

#### Scenario: Sandbox without the required facility

- **GIVEN** a host where the event-loop setup call is denied
- **WHEN** an end-to-end test starts
- **THEN** the test is skipped with a message naming the missing facility and how to run the suite
  where it is available
- **AND** the test does not attempt to spawn any process

#### Scenario: Supported host runs the suite

- **GIVEN** a host where the event-loop setup call succeeds
- **WHEN** an end-to-end test starts
- **THEN** the probe passes and the test proceeds to start its cluster

### Requirement: Default test graph participation

The end-to-end suite SHALL run as part of the repository's default test command and SHALL also be
runnable as a focused target.

#### Scenario: Default run includes the suite

- **GIVEN** a developer runs the repository's default test command
- **WHEN** the test graph is built
- **THEN** the end-to-end targets are included
- **AND** they skip rather than fail when the required facility is unavailable

#### Scenario: Focused run

- **GIVEN** a developer wants only the end-to-end coverage
- **WHEN** the dedicated end-to-end command is run
- **THEN** only the end-to-end targets execute
