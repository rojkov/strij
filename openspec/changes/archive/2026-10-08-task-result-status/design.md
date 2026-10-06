# Design

## Context

See `proposal.md` — Why. The constraints that shape the approach:

**The three existing error messages are admission outcomes, not task outcomes.** `TaskRejected` (frame `kTaskRejected = 5`) and `TaskDecline` (`kTaskDecline = 10`) both carry `{id, reason}` and describe a *scheduling* refusal. A scheduler may recover from either by picking another node. Only an unrecovered one reaches the user. This is why they keep a free-form `reason` and gain no status field.

**`ResultReceiver::DeliverError(std::string_view reason)` has no code, and its 20-odd call sites are where classification is lost.**

```
src/gateway/core/gateway_http_handler.cc:60   400, 404          per-site guesses
src/gateway/core/http_result_receiver.cc:72   503 for EVERY error   hardcoded
src/nodeagent/.../default_local_scheduler.cc:78  "no local capacity…"
src/gateway/.../probe_scheduler.cc:175          "…within the probe deadline"
```

**`HttpResponseFramer::ErrorResponse` is documented as *"Safe only before any result has been delivered"* and returns empty frames otherwise.** So a mid-stream failure has no representation today, and the new rule (first result only) matches that existing constraint rather than fighting it.

**`is_final` already sets the precedent for an optional field whose absence means something.** Absence means final, encoded by consumers as `!has_is_final() || is_final()`. `status` follows the same shape, and the two are independent: finality is about *which* result ends the task, status is about whether it succeeded.

**`gateway-task-bridge` R3 currently discards everything but body and finality** — it calls `Deliver(TaskResult.body, is_final)`. A status field is invisible until that requirement branches on it.

## Goals / Non-Goals

**Goals:**

- Distinguish retryable from non-retryable failure at the HTTP boundary, which is what the hardcoded 503 cannot do.
- Let the component that determines a failure classify it there, not where it is rendered.
- Treat the field's absence as success. This is a proto3 necessity rather than a compatibility measure: a producer that never sets an `optional` field omits it from the wire, so "unset" is reachable from a *current* producer and must read as success.
- A total code→HTTP mapping, so no input produces an unhandled case.

**Non-Goals:**

- Changing `TaskRejected`, `TaskDecline`, or their TLV type ids.
- Per-task-type error taxonomies. The codes describe *outcomes*, not handler vocabulary.
- Structured error bodies (error codes inside a JSON envelope). `body` stays bytes.
- A dead-letter or retry policy on the gateway. Making failures distinguishable is what a retry policy would consume; this change does not add one.

## Decisions

### D1 — `optional TaskStatus status = 4`, a protobuf enum, with zero *and* absence meaning OK

Mirrors `is_final = 3`'s precedent, so both optional fields read the same way in the schema and in consumer code. The code set is a `TaskStatus` enum declared in `api/common/task/task.proto`, so the constant names and the field type come from one declaration instead of a hand-maintained parallel table.

The two-ways-to-say-OK arrangement needs its rule stated as a single predicate, or it is ambiguous:

```
is_ok(result)  ==  !result.has_status() || result.status() == TASK_STATUS_OK
```

proto3 enums are open, so an unrecognized value still survives the round trip and arrives as a number the producer did not intend. That is handled by the predicate rather than by the field type: any value other than `TASK_STATUS_OK` — including one this version has no name for — reads as failure, and D2's mapping sends it to the internal-failure status. The failure mode worth guarding against is a `switch` with no `default` on a status that can be unknown, which is why the mapping is a single shared function whose `default` arm is the internal-failure status.

**Alternative rejected:** an `int32` field with separately declared constants. It buys nothing an enum does not, and costs the generated names — every call site would spell a number and depend on a comment for meaning, and the constants and the mapping table could drift apart.

### D2 — Eight codes, chosen by what the *caller* should do

The set is small on purpose; each code must be unambiguous about retryability. Values are enum members of `TaskStatus`, numbered so `TASK_STATUS_OK` is zero:

| Code | `TaskStatus` member | Meaning | HTTP | Rationale |
|---|---|---|---|---|
| 0 | `TASK_STATUS_OK` | OK | 200 | success |
| 1 | `TASK_STATUS_MALFORMED_REQUEST` | malformed/unusable request | 400 | not retryable unchanged |
| 2 | `TASK_STATUS_NOT_FOUND` | named thing does not exist | 404 | not retryable |
| 3 | `TASK_STATUS_UNIMPLEMENTED` | recognized but unimplemented | 501 | not retryable, permanent |
| 4 | `TASK_STATUS_DEADLINE_EXCEEDED` | deadline exceeded | 504 | retryable, but not the caller's fault |
| 5 | `TASK_STATUS_CAPACITY_REFUSED` | refused for capacity | 429 | retryable |
| 6 | `TASK_STATUS_INTERNAL` | internal failure | 500 | unknown retryability |
| 7 | `TASK_STATUS_UNAVAILABLE` | temporarily unable to serve | 503 | retryable, but not the caller's fault |

Codes 4 and 7 both map to retryable 5xx statuses but are distinct, because they call for different caller behavior on a retry: a deadline-exceeded task may be worth retrying once the downstream answers faster, while a temporarily-unavailable server is best left alone for a longer backoff. If no site ever needs that distinction, 7 can be dropped in favor of 6 — but collapsing them now would make the distinction unrecoverable later without renumbering.

Code 3 is included specifically for the `call: http` / `use.functions` / reserved-target case in the sibling `openworkflow-call-strij` change: "recognized but not yet supported" is a *permanent* answer, and lumping it into 500 would tell a caller to retry something that can never succeed.

Code 5 maps to 429: the caller sent a well-formed request and the answer is "not now." That is what 429 means, and it is retryable, so the caller's correct response — back off, retry later — follows from the status itself. 503 would also convey retryability but attributes the refusal to the server rather than to demand on it, which is the wrong cause for an exhausted pool: the node that refused is healthy, it simply has no free slot. A future change may add a distinct code for "this server is broken" if that distinction is needed, rather than overloading 503 onto an outcome that is really about load.

**Alternative rejected:** a code per error string. Free-form reasons are open-ended; a code set has to close, and closing it is what makes the mapping total.

### D3 — Only the first result may declare failure

A non-OK status on a non-first result is **ignored and logged as unexpected**.

This is not an arbitrary restriction — it is what HTTP forces. The status line is written once, at the first result, and cannot be revised. `HttpResponseFramer::ErrorResponse` already encodes this by returning empty frames once state has left `kIdle`. So the rule aligns the new field with a constraint the framer has had all along.

The consequence is stated rather than left implicit: **a task that fails mid-stream cannot report it as a status.** A handler that discovers a failure on its second result has to decide what it can still say. Two honest options exist — deliver the partial body as a normal result, or send a final result carrying a non-OK status that the gateway will log and ignore. The second is worse (it produces a log line per occurrence and communicates nothing), so the practical rule for implementers is: report failures on the first result. A `streaming-task-results` scenario pins the ignoring behavior; the choice is left to the handler.

**Alternative rejected:** carrying the error in a trailer or a final chunk envelope. HTTP trailers are not reliably available through this stack, and it would make the error body structurally different from a result body, complicating every consumer to fix a case that is rare.

### D4 — `DeliverError` gains a code, because the decision point is upstream of the renderer

The classification is known where the failure is determined and thrown away by the time it reaches the framer:

```
scheduler_router.cc:60       no eligible node        → 503?  404?  500?
probe_scheduler.cc:175       probe deadline exceeded → 504
result_receiver_storage.cc:18 "node disconnected"     → 503
```

Threading a code through `DeliverError` puts the classification at each of those sites. Without it the framer would have to infer a code from the reason *string*, which is the current failure mode with extra steps.

**Alternative rejected:** a `Result` free function that maps a reason substring to a code, called at the render site. That is string-matching a message to recover information the producer already had.

### D4a — `DeliverError` is not redundant with `Deliver`, and stays a separate method

Worth stating because the two look mergeable once `status` exists: `Deliver` can express a failure, so why keep a method whose only job is failure?

Two things keep them distinct.

**`Deliver` never gains a status parameter.** Its signature is `Deliver(std::span<const std::byte> value, bool is_final)`, where `value` is an already-serialized `TaskResult` treated as opaque bytes. Adding `status` to the proto places the code *inside* those bytes, so `Deliver`'s signature is unchanged by this change. There is no second parameter to absorb an error code into, and nothing about `Deliver` makes it a candidate for absorbing `DeliverError`.

**They emit different wire frames carrying different messages.** `Deliver` writes `kResult` (type_id 4) with a `TaskResult` payload. `DeliverError` writes `kTaskRejected` (type_id 5) with a `TaskRejected` payload — `id` and a free-form `reason`, built at `node_connection_result_receiver.cc:31`. Collapsing them would mean folding `TaskRejected` into `TaskResult`, which is the D5 boundary in reverse: the gateway would lose the distinction between "this node said no, I may pick another" and "the task ran and failed".

**Alternative rejected: unify all failures onto `kResult` with a non-OK status, dropping `DeliverError` and TLV type id 5.** It is coherent, and it is not cheap. The node agent sends `kTaskRejected` today from `RunTaskService`'s admission-rejection path (`run_task_service.cc:33`); making that a `kResult` would report a task that was never admitted as having run and failed, forcing `DefaultLocalScheduler`'s `kTaskRejected` arm, `RunTaskService`, and the probe scheduler's outcome demux to all become status-aware in order to recover the recovery distinction. It also moves work to the wrong side of the interface: every one of the ~20 `DeliverError` call sites would have to serialize a `TaskResult` to report a failure, instead of passing a code and a string. Two methods — one for a task outcome, one for an admission outcome — is the smaller surface.

The node-side consequence — no node can originate a code in this change — follows from D5 rather than from this decision, and is noted there.

### D5 — `TaskRejected` and `TaskDecline` stay reason-only, by design

Per the reasoning above: they are pre-execution scheduling decisions, recoverable by design. A code on them would imply a finality they lack — a client seeing `kTaskRejected` does not yet know the task failed, only that this node said no.

The unrecovered case is covered from the other side: when a scheduler exhausts its recovery options it calls `DeliverError` with a code (D4). The code is chosen at the *gateway*, not the node, because only the gateway knows whether recovery was attempted. `kTaskDecline` even has an explicit retry hint today (`retry-after`), which is the right shape for a recoverable refusal.

**Consequence to note:** the node still cannot say *why* it declined in machine-readable terms. A future change may add a code to `TaskDecline` if a scheduler needs to branch on the reason rather than just retry. Out of scope here.

## Risks / Trade-offs

**[Two sites choose different codes for the same underlying cause]** → sites name a `TaskStatus` member rather than a number, so the cost of two sites disagreeing is visible in review as two names. An unrecognized value still reads as a generic internal failure rather than as something else, so re-numbering a member would be a wire-visible break — mitigated by appending only, never renumbering, the same rule `typed-tlv-messages` R4 already follows for TLV type ids.

**[Mid-stream failures are unreportable]** → inherent to HTTP's one-status-line model, and the framer already refused to emit after the first result. Mitigated by logging rather than silently dropping, and by a scenario pinning the behavior so a handler author finds it in the spec rather than in production logs.

**[A consumer switches on a status it does not know]** → proto3 enums are open, so an unrecognized value can arrive from a newer producer. The D1 predicate makes every non-zero member a failure, and the spec requires an unnamed value to map to the internal-failure HTTP status. A consumer cannot accidentally treat it as success. The mitigation lives in the shared mapping's `default` arm rather than in each consumer's `switch`.

**[The two-ways-to-say-OK predicate is misapplied somewhere]** → the predicate is written once in the spec and used verbatim by the handler and the framer. Covered by a scenario asserting a `TaskResult` with no status parses as a success, and one asserting an unnamed member still parses as a failure.

**[`DeliverError`'s signature change ripples to ~20 call sites and mocks]** → mechanical, and the compiler finds all of them. `test/mocks/` implementations of `ResultReceiver` need the extra parameter.

## Migration Plan

None. There are no deployments to migrate. The wire change is additive and needs no coordination, and the only pre-existing behavior it alters is the HTTP status of an error, which is the point of the change.

A gateway whose producers do not yet set a code still behaves correctly: those failures fall through to the internal-failure mapping. Imprecise, not wrong, so the two halves can land in either order within a single change.

## Settled, Not Revisited

- **The code set is a protobuf enum, not an integral field.** `TaskStatus` lives in `api/common/task/task.proto` beside `TaskResult`, and `status` is `optional TaskStatus status = 4`. The names and the mapping table come from one declaration, so there is no parallel constant table to drift. The open-enum risk is handled by the D1 predicate and the mapping's `default` arm, not by avoiding the enum.
- **The code→HTTP mapping is fixed, not configurable, in v1.** Recorded in D2 as a decision rather than an open question. An operator cannot choose 429 over 503 per pool; the table in D2 is the whole policy. Making it configurable would move the mapping out of the shared function in `src/common/core/utils/` and into config, and would give every deployment a way to be wrong about retry semantics. If a future change needs it, that change owns the decision — do not pre-build the seam.
