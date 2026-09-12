## 1. Node foundation (4A): child submission seam

- [ ] 1.1 Change `NodeAgentConfig.schedulers` to `repeated SchedulerConfig { ExtensionConfig extension; string task_type; }` in `api/nodeagent/config/nodeagent.proto` (empty `task_type` = default); update the config loader/validation to reject empty lists, unknown names, duplicate non-empty task types, and multiple defaults.
- [ ] 1.2 Implement `LocalReceiverRegistry` (node-side mirror of `gateway::ResultReceiverStorage`): `Put(task_id, ReceiverPtr)`, `Get`, `Erase`, `Empty`, `Size`; core-owned, in `src/nodeagent/core/`.
- [ ] 1.3 Implement `RegistryResultSender` (a `nodeagent::ResultSender` bound to the registry + fixed `task_id`): `Send` resolves the receiver by id, delivers body/finality, erases on final; lifecycle hooks supported.
- [ ] 1.4 Add sender-backed `RunTaskService` overloads: `RunTask(const Task&, ResultSenderPtr)` (admitting) and `RunTask(const Task&, ResultSenderPtr, AdmissionScopePtr reserved)` (preallocated); keep the existing `io::Connection`-bound overloads as thin wrappers delegating to the shared path.
- [ ] 1.5 Implement `ChildSchedulerRouter` (node-side `extensions::Scheduler` composite mirroring the gateway `SchedulerRouter`): `Schedule` dispatches by `child.type()` to the `task_type`-matched local scheduler, default fallback, error when no match and no default.
- [ ] 1.6 Define the narrow `ChildTaskSubmitter` interface (`Submit(const Task&, gateway::ResultReceiverPtr)`) as the node-global shared instance wrapping the `ChildSchedulerRouter`; add `ChildTaskSubmitter()` accessor to `NodeagentFactoryContext` (via `SetChildTaskSubmitter` two-phase install in the impl).
- [ ] 1.7 Implement the shared child-policy step (used by local schedulers' `Schedule`): attempt `AdmissionController::Admit` → on success run via sender-backed `RunTask` with a `RegistryResultSender`; on failure take the forward path (4B) or, when forwarding is unavailable, deliver an error to the receiver.
- [ ] 1.8 Implement `PushLocalScheduler::Schedule` using the child-policy step (replaces its error-delivery body); add a local-run test proving the child's result reaches a registry receiver.
- [ ] 1.9 Implement `ProbeLocalScheduler::Schedule` using the child-policy step with NO queueing/probe frames; add a test proving a full probe queue forwards (or errors) instead of enqueuing.
- [ ] 1.10 Add core `kResult` and `kTaskRejected` cases to `NodeagentTlvHandler`: resolve id in `LocalReceiverRegistry`, deliver (final erases), drop unknown ids with a warning; these type_ids stay out of every scheduler's `HandledFrameTypes()`.
- [ ] 1.11 Add the example workflow task handler (`"workflow"` factory in `Registry<TaskHandlerFactory>`): factory retrieves `context.ChildTaskSubmitter()`, passes to handler ctor; handler fans out to child tasks with `GenerateTaskId()` ids, aggregates final bodies into the parent result, and aborts on a child error.
- [ ] 1.12 Wire `nodeagent_framework.cc`: build schedulers, build `ChildSchedulerRouter` + submitter, registry, install into `NodeagentFactoryContext`, pass registry to each `NodeagentTlvHandler`.
- [ ] 1.13 Tests for 4A: registry put/get/erase; `RegistryResultSender` delivery; sender-backed `RunTask` variants (admit, preallocated-release, no-handler drop); `ChildSchedulerRouter` (type match, default, no-default error); dispatcher `kResult`/`kTaskRejected` routing incl. unknown id; config schema + ambiguity validation; workflow handler fan-out/aggregate/abort; `echo`/`piped_executable` handlers and their tests unchanged (verification of the locked interface decision).

## 2. Forward path (4B): GatewayClient

- [ ] 2.1 Add `NodeAgentConfig.gateway_client { repeated string addresses; }` to the proto; optional section, valid-endpoint validation.
- [ ] 2.2 Implement `GatewayClient` (exposed via `NodeagentFactoryContext`): `Submit(Task, ResultReceiverPtr)` writes an upstream `kTaskSubmission` frame; `RegisterConnection(mailbox)` lets the node's accepted connections register with it; FIFO round-robin over live connections; async `PrepareConnect` dial fallback across `gateway_client.addresses`; error to the receiver when nothing is reachable.
- [ ] 2.3 Wire `nodeagent_framework.cc`: create the `GatewayClient`, register each accepted connection on accept, install into the context (before schedulers with a forward path are built).
- [ ] 2.4 Tests: write-on-live-connection; round-robin across N live connections; dial fallback when none live; queued-submission-after-connect; all-addresses-fail delivers error; empty addresses + no live connection delivers error immediately.

## 3. Gateway inbound (4B): upstream child routing

- [ ] 3.1 Implement `NodeConnectionResultReceiver` (TLV sibling of `HttpResultReceiver`): `Deliver`/`DeliverError` write `kResult`/`kTaskRejected` frames on the connection's mailbox.
- [ ] 3.2 Teach `SchedulerRouter` to claim `kTaskSubmission` (`HandledFrameTypes()` + `HandleFrame`): parse `task::Task`, build + store a `NodeConnectionResultReceiver` keyed by task id and submitting node id, dispatch through normal `Schedule` routing; unmatched-with-no-default → `DeliverError`.
- [ ] 3.3 Verify `GatewayTlvHandler`/`gateway_framework.cc` require no new wiring (default seam already routes to the router; `NotifyNodeDisconnected` already cleans up by node id); adjust if the seam needs a status contract tweak for claimed types.
- [ ] 3.4 Tests: upstream submission routes by type; unmatched upstream child writes `kTaskRejected` back; node disconnect cleans pending child receivers; full two-hop end-to-end (parent on node A → gateway → worker → gateway → node A registry → parent), including capacity release on the worker and registry erasure on node A.

## 4. Build and verification

- [ ] 4.1 Update BUILD.bazel targets/deps for the new nodeagent core components, the workflow example handler, and the gateway receiver/router changes.
- [ ] 4.2 Run `make build` and `make test`; fix failures; run `make clang-tidy` on changed files and `make check`.
- [ ] 4.3 Run `openspec validate` on the change; confirm spec deltas apply cleanly against main specs.
- [ ] 4.4 Update `AGENTS.md` architecture notes: construction-injected child submitter, child router, local registry, `GatewayClient`, gateway upstream-child routing, and the "interface unchanged" constraint.