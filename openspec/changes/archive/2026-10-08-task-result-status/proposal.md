# Proposal

## Why

Task failures carry no code anywhere in the protocol. A task that fails and a task that is refused because a GPU pool is exhausted both arrive at the client as an undifferentiated error, so every consumer guesses:

```
src/gateway/core/http_result_receiver.cc:72   503 Service Unavailable  (hardcoded, for EVERY error)
src/gateway/core/gateway_http_handler.cc:60   400, 404                 (per-site guesses)
```

`HttpResponseFramer::ErrorResponse` emits `503` for every failure, so a malformed workflow request and an exhausted pool are indistinguishable to the caller. A caller that wants to retry, a caller that wants to fix its request, and a caller watching for capacity pressure all see the same status.

## What Changes

- **`TaskResult.status`** — a new `optional` field of enum type `TaskStatus`, declared beside `TaskResult` in `api/common/task/task.proto`. The enum's zero member means OK, so absence or `TASK_STATUS_OK` mean success. Any other value is a failure, and `body` carries the error message.
- **A fixed, small code set** that maps to HTTP status codes, declared as protobuf enum members on `TaskStatus`. The mapping is total: every declared code has an HTTP counterpart, and an unassigned code still reads as a failure (never as OK), mapping to 500.
- **The gateway translates status to HTTP.** `GatewayTlvHandler` inspects the field, and `HttpResponseFramer` writes the mapped HTTP status instead of a hardcoded 200/503.
- **A non-OK status is only honored on the first result of a task.** HTTP response framing is decided at the first result, so a failure signalled on a later chunk cannot change a status line already sent. Such a status SHALL be ignored and logged as unexpected.
- **`ResultReceiver::DeliverError` gains a code**, so the gateway components that determine a failure — the scheduler router (admission refused, no eligible node), the probe scheduler (deadline exceeded), the receiver storage (node disconnected) — classify it where they detect it, rather than the renderer inferring one from reason text.
- **`DeliverError` is kept, not folded into `Deliver`.** This looks redundant once `status` exists, so it is stated explicitly: `Deliver` takes an opaque serialized `TaskResult`, so the code travels *inside* the payload and `Deliver`'s signature does not change; the two methods also write different frames (`kResult` vs `kTaskRejected`) carrying different messages (`TaskResult` vs `TaskRejected`), which keeps the admission-outcome boundary intact. Unifying them would move node-side demux into this change for no gain. Rationale in design D4a.
- **`TaskRejected` and `TaskDecline` are deliberately left alone.** They describe admission outcomes, not task outcomes: a scheduler may recover from either by picking another node, and only an unrecovered one reaches the user as a failure. Their free-form `reason` is the right shape for a log line at the point of decision, and adding a code to them would imply a finality they do not have.

No wire-format break: `status` is a new optional field, and nothing about `is_final`'s absence-means-final rule changes. There are no deployments to keep compatible with, so the change is free to alter the existing error surface — most of it does, since previously every error was HTTP 503.

## Capabilities

### New Capabilities

- `task-result-status`: the `status` field and its code set, the rule that only the first result may carry a non-OK status, and the classification of failures at the point they are produced.

### Modified Capabilities

- `task-protocol`: `TaskResult` gains the `status` field. The requirement enumerates the message's fields exhaustively, so it must be restated with the addition.
- `gateway-task-bridge`: the handler currently calls `receiver->Deliver(TaskResult.body, is_final)` and drops everything else; it must inspect `status` and route a failure to `DeliverError` with the code.
- `streaming-task-results`: HTTP framing is decided at the first result and the success path hardcodes `200 OK`; a non-OK status on that first result changes the status line written.

## Impact

**New code**

- `api/common/task/task.proto` — the `TaskStatus` enum and the `status` field.
- `src/gateway/core/http_result_receiver.cc` — code-to-HTTP translation in `HttpResponseFramer`.
- `include/strij/gateway/result_receiver_storage.hh` — `DeliverError` gains a code parameter.
- `src/common/core/utils/` — the code-to-HTTP mapping as a shared, testable function.

**Touched code**

- `src/gateway/core/gateway_tlv_handler.cc` — inspect `status` on the parsed `TaskResult`.
- `src/gateway/core/result_receiver_storage.cc`, `src/gateway/core/scheduler_router/scheduler_router.cc`, `src/nodeagent/core/nodeagent_scheduler_router.cc`, the node and gateway local schedulers, and the node-connection receiver — every `DeliverError` call site gains a code.
- `src/nodeagent/extensions/task_handlers/openworkflow/` — the workflow handler sets a code on failure, replacing the bare reason (it is the first producer with a meaningful classification).

**Upstream dependencies (unchanged contracts, new consumers)**

- `child-result-routing` — `DeliverError` signature change is a delta against `ResultReceiver`. The two-hop path's success payloads are unaffected: a child's result crosses the wire as an unmodified `TaskResult`. The two-hop *failure* path, however, does **not** carry the code upstream. By design D4a/D5, `NodeConnectionResultReceiver::DeliverError` writes a reason-only `kTaskRejected` (refusal vs failure distinction, pinned by `child-result-routing`), so a child's status is lost at the gateway hop and the submitting node classifies the failure from the reason string alone. Only failures that reach an HTTP client directly carry the code end-to-end.
- `receiver-connection-lifecycle` — a node disconnect now delivers a specific code instead of a bare reason.
