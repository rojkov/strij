## 1. Node foundation (4A): child submission seam

- [x] 1.1 Introduce `repeated NodeSchedulerConfig { ExtensionConfig extension; string task_type; bool local_default; }` in `api/nodeagent/config/nodeagent.proto` (node-side shape, distinct from the gateway `SchedulerConfig`); update the config loader/validation to reject empty lists, unknown names, duplicate non-empty `task_type`s, more than one `local_default`, and to treat an empty `task_type` as NO local role (not a default).
- [x] 1.2 Implement `LocalReceiverRegistry` (node-side mirror of `gateway::ResultReceiverStorage`): `Put(task_id, ReceiverPtr)`, `Get`, `Erase`, `Empty`, `Size`; core-owned, in `src/nodeagent/core/`.
- [x] 1.3 Implement `RegistryResultSender` (a `nodeagent::ResultSender` bound to the registry + fixed `task_id`): `Send` resolves the receiver by id, delivers body/finality, erases on final; lifecycle hooks supported.
- [x] 1.4 Add sender-backed `RunTaskService` overloads: `RunTask(const Task&, ResultSenderPtr)` (admitting) and `RunTask(const Task&, ResultSenderPtr, AdmissionScopePtr reserved)` (preallocated); keep the existing `io::Connection`-bound overloads as thin wrappers delegating to the shared path.
- [x] 1.5 Implement `ChildSchedulerRouter` (node-side `extensions::Scheduler` composite): `Schedule` dispatches by `child.type()` to the entry declaring that `task_type`, falls back to the single `local_default` entry, and delivers an error when nothing claims the type and no local default is declared; entries with no local role never receive submissions.
- [x] 1.6 Define the narrow `ChildTaskSubmitter` interface (`Submit(const Task&, gateway::ResultReceiverPtr)`) as the node-global shared instance wrapping the `ChildSchedulerRouter`; add `ChildTaskSubmitter()` accessor to `NodeagentFactoryContext` (via `SetChildTaskSubmitter` two-phase install in the impl).
- [x] 1.7 Implement the shared child-policy step (used by local schedulers' `Schedule`): attempt `AdmissionController::Admit` → on success run via sender-backed `RunTask` with a `RegistryResultSender`; on failure take the forward path (4B) or, when forwarding is unavailable, deliver an error to the receiver.
- [x] 1.8 Implement `PushLocalScheduler::Schedule` using the child-policy step (replaces its error-delivery body); add a local-run test proving the child's result reaches a registry receiver.
- [x] 1.9 Implement `ProbeLocalScheduler::Schedule` using the child-policy step with NO queueing/probe frames; add a test proving a full probe queue forwards (or errors) instead of enqueuing.
- [x] 1.10 Add core `kResult` and `kTaskRejected` cases to `NodeagentTlvHandler`: resolve id in `LocalReceiverRegistry`, deliver (final erases), drop unknown ids with a warning; these type_ids stay out of every scheduler's `HandledFrameTypes()`.
- [x] 1.11 Add the example workflow task handler (`"workflow"` factory in `Registry<TaskHandlerFactory>`): factory retrieves `context.ChildTaskSubmitter()`, passes to handler ctor; handler fans out to child tasks with `GenerateTaskId()` ids, aggregates final bodies into the parent result, and aborts on a child error.
- [x] 1.12 Wire `nodeagent_framework.cc`: build schedulers (applying the `task_type`/`local_default` role declarations), build `ChildSchedulerRouter` + submitter, registry, install into `NodeagentFactoryContext`, pass registry to each `NodeagentTlvHandler`.
- [x] 1.13 Tests for 4A: registry put/get/erase; `RegistryResultSender` delivery; sender-backed `RunTask` variants (admit, preallocated-release, no-handler drop); `ChildSchedulerRouter` (type claim, explicit local-default fallback, no-claim-without-default error, no-role entry never scheduled); dispatcher `kResult`/`kTaskRejected` routing incl. unknown id; config schema + role validation (duplicate `task_type`, multiple `local_default`, empty `task_type` ≠ default); workflow handler fan-out/aggregate/abort; `echo`/`piped_executable` handlers and their tests unchanged (verification of the locked interface decision).

## 2. Forward path (4B): GatewayClient

- [x] 2.1 Add `NodeAgentConfig.gateway_client { repeated string addresses; }` to the proto; optional section, valid-endpoint validation.
- [x] 2.2 Implement `GatewayClient` (exposed via `NodeagentFactoryContext`): a `ChildForwarder` whose `Forward(Task)` writes an upstream `kTaskSubmission` frame; `RegisterConnection(mailbox)` lets the node's accepted connections register with it; FIFO round-robin over live connections; error (non-Ok `Forward`) when nothing is reachable.
- [x] 2.3 Wire `nodeagent_framework.cc`: create the `GatewayClient`, register each accepted connection on accept, install into the context (before schedulers with a forward path are built).
- [x] 2.4 Tests: write-on-live-connection; round-robin across N live connections; no live connection → non-Ok `Forward` (parent error via the forward path).

## 3. Gateway inbound (4B): upstream child routing

- [x] 3.1 Implement `NodeConnectionResultReceiver` (TLV sibling of `HttpResultReceiver`): `Deliver`/`DeliverError` write `kResult`/`kTaskRejected` frames on the connection's mailbox.
- [x] 3.2 Teach `SchedulerRouter` to claim `kTaskSubmission` (`HandledFrameTypes()` + `HandleFrame`): parse `task::Task`, build + store a `NodeConnectionResultReceiver` keyed by task id and submitting node id, dispatch through normal `Schedule` routing; unmatched-with-no-default → `DeliverError`.
- [x] 3.3 Verify `GatewayTlvHandler`/`gateway_framework.cc` require no new wiring (default seam already routes to the router; `NotifyNodeDisconnected` already cleans up by node id); adjust if the seam needs a status contract tweak for claimed types.
- [x] 3.4 Tests: upstream submission routes by type; unmatched upstream child writes `kTaskRejected` back; node disconnect cleans pending child receivers; two-hop path covered by composition — gateway-side routing/storage/receiver tests + node-side `GatewayClient` and registry delivery tests (capacity release on the worker via `AdmissionScope`, registry erasure on node A).

## 4. Build and verification

- [x] 4.1 Update BUILD.bazel targets/deps for the new nodeagent core components, the workflow example handler, and the gateway receiver/router changes.
- [x] 4.2 Run `make build` and `make test`; fix failures; run `make check`.
- [x] 4.3 Run `openspec validate` on the change; confirm spec deltas apply cleanly against main specs.
- [x] 4.4 Update `AGENTS.md` architecture notes: construction-injected child submitter, child router, local registry, `GatewayClient`, gateway upstream-child routing, and the "interface unchanged" constraint.