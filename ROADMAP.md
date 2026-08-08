# Roadmap

Follow-ups gathered from the OpenSpec specs and archived change designs (`openspec/specs/`, `openspec/changes/archive/`) and from `TODO`/`FIXME` markers in the code. Ordered by recommended implementation order: correctness/hygiene first, then feature maturity, then platform-scale work.

## Phase 1 — Correctness & hygiene (do next)

- [ ] **Unify teardown: migrate `CLOSE_CONNECTION` onto `DEFERRED_DELETE`**
  - Migrate the pre-existing `Command::CLOSE_CONNECTION` usage to the generic `DEFERRED_DELETE` command and document the deferred-delete pattern.
  - Source: `openspec/changes/archive/2026-08-08-piped-executable-task-handler/proposal.md`, `design.md` (D6). Mechanical, removes a redundant `Command::Type` value.

- [ ] **Gateway receiver lifecycle: clean up `ResultReceiverStorage` on connection drop**
  - `ResultReceiverStorage` currently has no cleanup when connections drop, leaking receivers for in-flight async tasks.
  - Source: TODO `src/core/gateway/result_receiver_storage.hh:40`; overlaps the piped_executable follow-up "per-task cancellation + result-receiver lifecycle on connection drop".

- [ ] **`TcpListener`: guard against shutdown in progress**
  - Accept path does not check whether shutdown is underway.
  - Source: TODO `src/core/io/tcp_listener.cc:56`.

- [ ] **Resolve whether `FactoryContext::Logger()` is needed**
  - Either remove the interface method or justify it; it is currently unused by extensions.
  - Source: TODO `src/core/extensions/factory_context.hh:17`.

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

- [ ] **Reconnection logic for disconnected nodes**
  - Disconnected nodes currently stay disconnected.
  - Source: `openspec/changes/archive/2026-07-25-node-directory/design.md`.

- [ ] **Runtime node discovery**
  - First-class `Node` objects with status tracking so a discovery mechanism can add/remove nodes at runtime.
  - Source: `openspec/changes/archive/2026-07-25-node-directory/proposal.md`.

- [ ] **Pluggable scheduler**
  - Route tasks by node capability and load instead of round-robin. Depends on runtime node discovery.
  - Source: `openspec/changes/archive/2026-07-25-node-directory/proposal.md` / `design.md`.

- [ ] **Concurrency / resource limiting**
  - Gateway-side resource accounting and routing limits.
  - Source: `openspec/changes/archive/2026-08-08-piped-executable-task-handler/proposal.md` (non-goal).

## Phase 4 — Protocol & observability

- [ ] **Supported-type advertisement and `GET /tasks` listing**
  - Nodeagents advertise supported task types; gateway validates unknown task types; optional `GET /tasks` endpoint.
  - Source: `openspec/changes/archive/2026-07-31-structured-tasks-protobuf/proposal.md`.

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
  - `connection_timeout`, `request_timeout`, `heartbeat_interval`, `max_connections`, `SO_REUSEPORT`. Some overlap with the piped_executable task timeout.
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
