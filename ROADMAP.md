# Roadmap

Follow-ups gathered from the OpenSpec specs and archived change designs (`openspec/specs/`, `openspec/changes/archive/`) and from `TODO`/`FIXME` markers in the code. Ordered by recommended implementation order: correctness/hygiene first, then feature maturity, then platform-scale work.

The `2026-08-19-node-capabilities-and-scheduling` change has landed the node-management spine (stable `node_id`, dynamic `NodeDirectory`, `kNodeAdvertisement`/`kNodeState`/`kTaskRejected` frames, named-pool resources, admission control, and the `round_robin`/`capability_aware` schedulers); its deferred seams and open questions are listed in Phase 3.

## Phase 1 — Correctness & hygiene (do next)

- [x] **Gateway receiver lifecycle: clean up `ResultReceiverStorage` on connection drop**
  - `ResultReceiverStorage` currently has no cleanup when connections drop, leaking receivers for in-flight async tasks.
  - Source: TODO `src/core/gateway/result_receiver_storage.hh:40`; overlaps the piped_executable follow-up "per-task cancellation + result-receiver lifecycle on connection drop".

- [ ] **io_uring write-after-close race (SIGPIPE)**
  - Within a single io_uring CQE batch, a handler may submit a write SQE (e.g. delivering a task result to an HTTP connection) and then the same batch contains the EOF for that connection, closing the fd. The stale SQE is submitted on the next `io_uring_submit_and_wait`, causing the kernel to deliver SIGPIPE. Currently masked by `signal(SIGPIPE, SIG_IGN)` in `gateway.cc` — the write completion handler already treats `res <= 0` as an error and tears down the connection. The proper fix is to defer SQE submission: queue write data in `Connection::Write()` without calling `PrepareWrite`, and submit all pending writes after the CQE batch is drained. The same race applies to node disconnect paths where `NotifyNodeDisconnected` writes to HTTP connections. Affects both gateway and nodeagent.
  - Source: SIGPIPE crash reported during HTTP client drop with in-flight task; `src/core/io/connection.cc:60`, `src/core/event/dispatcher_impl.cc:47`.

- [ ] **`TcpListener`: guard against shutdown in progress**
  - Accept path does not check whether shutdown is underway.
  - Source: TODO `src/core/io/tcp_listener.cc:56`.

- [ ] **Resolve whether `FactoryContext::Logger()` is needed**
  - Either remove the interface method or justify it; it is currently unused by extensions.
  - Source: TODO `src/core/extensions/factory_context.hh:17`.

- [ ] **Relocate `LocalFunctionResolver` into the nodeagent namespace**
  - The class conceptually belongs to `strij::nodeagent` but resides in `src/core/extensions`. Consider moving it to `src/nodeagent`.
  - Source: TODO `src/core/extensions/function_resolver.hh:44`.

- [ ] **Make the `FactoryContextImpl` `function_resolver` argument non-optional**
  - Always pass a resolver (even a `LocalFunctionResolver`) to avoid potential null dereferences; may imply having different factory context implementations for gateway and nodeagent.
  - Source: TODO `src/core/extensions/factory_context.hh:34`.

## Phase 2 — piped_executable maturity

- [ ] **Surface errors/exit codes via a `TaskResult` error field**
  - Spawn failures currently deliver an empty final result indistinguishable from empty stdout; non-zero exit codes are never surfaced. Add an error/exit-code field to `TaskResult`, plumb from `ChildProcess`.
  - Source: `openspec/changes/archive/2026-08-08-piped-executable-task-handler/proposal.md` / `design.md` (non-goal).

- [ ] **Task timeouts**
  - A child that never exits pins the HTTP connection open indefinitely.
  - Source: `openspec/changes/archive/2026-08-08-piped-executable-task-handler/proposal.md` / `design.md` (risk table).

- [ ] **Bound the write queue (backpressure) for slow clients / unbounded child output**
  - The outbound mailbox write queue grows without bound; streaming amplified the exposure. Revisit backpressure now that streaming has landed.
  - Source: `openspec/changes/archive/2026-08-08-piped-executable-task-handler/design.md` (risk table), `openspec/changes/archive/2026-08-05-async-result-sender-mailbox/design.md` (Open Questions).

- [ ] **Handle output written by grandchildren after the drain**
  - stdout data from grandchildren written after the exit drain can be truncated (known v1 edge).
  - Source: `openspec/changes/archive/2026-08-08-piped-executable-task-handler/design.md` (risk table).

- [ ] **Executable allowlisting / security hardening**
  - Restrict which binaries a `function` parameter may invoke (future deployment extension).
  - Source: `openspec/changes/archive/2026-08-08-piped-executable-task-handler/design.md` (non-goal).

## Phase 3 — Node management (async-first)

The `2026-08-19-node-capabilities-and-scheduling` change landed runtime node discovery, the pluggable `Scheduler` extension family, and gateway/nodeagent resource accounting (marked `[x]` below). What remains is reconnection, the deferred seams from that design, and its open questions.

- [x] **Runtime node discovery**
  - Landed via `2026-08-19-node-capabilities-and-scheduling`: `NodeDirectory` adds/removes/updates nodes from repeated discovery snapshots; `NodeInfo` carries a stable `node_id`.
  - Source: `openspec/specs/dynamic-node-directory/spec.md`; `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D3, D4).

- [x] **Pluggable scheduler**
  - Landed via `2026-08-19-node-capabilities-and-scheduling`: `Scheduler` extension family (`round_robin`, `capability_aware`) with `scheduling_protocols`-based candidate filtering.
  - Source: `openspec/specs/pluggable-scheduler/spec.md`; `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D12).

- [x] **Concurrency / resource limiting**
  - Landed via `2026-08-19-node-capabilities-and-scheduling`: nodeagent pool + per-type admission control with `kTaskRejected`, gateway `ExactStateTracker` and `capability_aware` filtering.
  - Source: `openspec/specs/node-state-reporting/spec.md`; `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D9-D11).

- [ ] **Reconnection logic for disconnected nodes**
  - Disconnected nodes currently stay disconnected; the node-capabilities change handles removal, not reconnect-backoff.
  - Source: `openspec/changes/archive/2026-07-25-node-directory/design.md`, `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (non-goal).

- [ ] **New discovery channels**
  - Only `static` ships; add Redis, mDNS, ZooKeeper, DNS SRV `NodeDiscovery` implementations. The discovery channel carries existence only (node_id + address).
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D1, non-goal); `openspec/specs/node-discovery/spec.md`.

- [ ] **Two-sided (Sparrow-style) scheduling**
  - Probe/lease/pull protocols behind the `scheduling_protocols` seam: nodeagent-side extensions register a protocol handler (e.g. `probe`) and advertise it; gateways exclude nodes not advertising a scheduler's required protocol (mixed-fleet rollout).
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D8, non-goal).

- [ ] **Late-binding scheduler interface**
  - A Sparrow-like scheduler cannot `Choose()` a node up front; replace the sync interface with fire-and-forget `Schedule(std::move(task))` plus a dispatch callback and a completion hook (`kResult`/`kTaskRejected` notifying the scheduler when a node frees capacity). The `nullptr → 503` contract is v1-only and must not apply to such a scheduler.
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D12, Open Questions).

- [ ] **Nodeagent enqueue-based admission**
  - Enqueue incoming tasks when capacity is exhausted and reject only when the queue overflows (Sparrow-style), instead of immediate `kTaskRejected`.
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D11).

- [ ] **Function repository (function plane)**
  - Real `RequirementsResolver`/`CodeResolver` implementations behind the D13 seam: function plane carries `(type, function_id) → reqs`, code, and vetting flags; the nodeagent `FunctionResolver` evolves into a code-fetching resolver. Filesystem validation and executable allowlisting also live here (overlaps Phase 2 allowlisting).
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D2, D13); `openspec/specs/function-resolver/spec.md`.

- [ ] **Cached/distributed state model**
  - Dual-mode design (D10): cached estimates + TTL corrected by rejections, with probe-based self-selection, for multi-gateway deployments; v1 `ExactStateTracker` is exact only for a single gateway.
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D10).

- [ ] **Inbound nodeagent-initiated connections (`NodeAnnouncer`)**
  - The "direct announcement" deployment where the gateway acts as a listener; v1 keeps the outbound-only model.
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (non-goal).

- [ ] **Non-heartbeat state channels + staleness TTL**
  - Redis/ZK pub-sub `update_channels` (self-describing descriptors); when state does not ride the connection, define a TTL/eviction rule for stale node state.
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D7, D9, Open Questions).

- [ ] **Persist `node_id` across restarts**
  - v1 generates a stable in-memory `node_id` at startup; persistence would keep identity across restarts (addresses can still churn).
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D3).

- [ ] **Pool-source extension category**
  - v1 requires pools to be declared in `NodeAgentConfig`; add auto-probing sources (e.g. `auto_probe_cadvisor`) behind a `static_config`-style extension point.
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (D15); `openspec/specs/nodeagent-config/spec.md`.

- [ ] **`function_sourced` handler semantics**
  - How handlers that accept function IDs declare themselves, and how `default_resources` interacts with repo-resolved requirements when both exist.
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (Open Questions).

- [ ] **Reject retry policy**
  - Whether any v1 scheduler policy auto-retries a `kTaskRejected` task on another node (default: no).
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (Open Questions).

- [ ] **Subscription negotiation / advertisement dedup**
  - Avoid re-sending the full advertisement on every connection and dedup `kNodeState` broadcasts across many gateways.
  - Source: `openspec/changes/archive/2026-08-19-node-capabilities-and-scheduling/design.md` (risks).

## Phase 4 — Protocol & observability

- [ ] **Supported-type advertisement and `GET /tasks` listing**
  - Nodeagents advertise supported task types; gateway validates unknown task types; optional `GET /tasks` endpoint.
  - Source: `openspec/changes/archive/2026-07-31-structured-tasks-protobuf/proposal.md`. Nodeagent handler capabilities are now advertised via `HandlerCapability` (`kNodeAdvertisement`), so this reduces to gateway-side validation + the listing endpoint.

- [ ] **Runtime task-handler reconfiguration**
  - `TaskHandlerManager` already exposes `AddHandler(type, handler)` / `RemoveHandler(type)` as a seam, but the enabled set comes only from configuration; no runtime reconfiguration path is wired. Empty config currently logs a warning and continues with a manager routing nothing.
  - Source: `openspec/specs/nodeagent-task-handlers/spec.md`.

- [ ] **Process supervision**
  - Supervise spawned processes beyond the per-task child.
  - Source: `openspec/changes/archive/2026-08-05-async-result-sender-mailbox/design.md` (Open Questions).

- [ ] **Optional `writev` batching for result frames**
  - Batch TLV result frames to reduce syscalls.
  - Source: `openspec/changes/archive/2026-08-05-async-result-sender-mailbox/design.md` (Open Questions).

## Phase 5 — Config & platform features

- [ ] **Config hot-reload (SIGHUP)**
  - Source: `openspec/specs/yaml-config-loader/spec.md` (Out of Scope).

- [ ] **Timeouts & connection limits**
  - `connection_timeout`, `request_timeout`, `max_connections`, `SO_REUSEPORT`. `heartbeat_interval` is now active as the `kNodeState` cadence (default 10s). Some overlap with the piped_executable task timeout.
  - Source: `openspec/changes/archive/2026-07-23-yaml-protobuf-config/tasks.md` (Future Work).

- [ ] **Reconnect knobs**
  - `max_reconnect_attempts`, `reconnect_backoff_ms`. Depends on Phase 3 reconnection.
  - Source: `openspec/changes/archive/2026-07-23-yaml-protobuf-config/proposal.md`.

- [ ] **File logging**
  - `logging.output = "file"` with `file_path`.
  - Source: `openspec/changes/archive/2026-07-23-yaml-protobuf-config/proposal.md`.

- [ ] **TLS support**
  - TcpListener TLS, `TlsConfig` fields, cert/key/ca loading and validation, verify_peer, K8s secret examples.
  - Source: `openspec/changes/archive/2026-07-23-yaml-protobuf-config/tasks.md` (Future Work).

- [ ] **Config ecosystem**
  - Encryption/secrets, remote config (etcd/Consul), versioning/migration, JSON config format, schema documentation generator.
  - Source: `openspec/specs/yaml-config-loader/spec.md` (Out of Scope).

## Phase 6 — Tooling

- [ ] **clang-tidy tooling**
  - Integrate `tools/run-clang-tidy.py` with clang-tidy-diff; simplify the Python 3.12 semaphore usage.
  - Source: FIXMEs `tools/run-clang-tidy.py:10`, `tools/run-clang-tidy.py:339`.
