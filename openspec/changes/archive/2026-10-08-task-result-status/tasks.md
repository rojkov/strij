# Tasks

## 1. Status codes and the protocol field

- [x] 1.1 Declare `TaskStatus` in `api/common/task/task.proto` with the eight members from design D2, zero being `TASK_STATUS_OK`, and the single success predicate `!has_status() || status() == TASK_STATUS_OK`; verify the predicate reads a `TaskResult` with no status, with `TASK_STATUS_OK`, and with an unnamed non-zero value as success, success, and failure respectively
- [x] 1.2 Add `optional TaskStatus status = 4` to `TaskResult` in `api/common/task/task.proto` with a comment stating the absence/zero-member/failure rule and that `body` carries the error message when not OK; verify a `TaskResult` serialized without the field deserializes without `has_status()`, and that a member number this schema does not name survives the round trip and still reads as a failure
- [x] 1.3 Verify the field number does not collide with existing `TaskResult` fields and that no TLV type id or other message changes; verify `bazel build //...` succeeds and existing `task-protocol` tests pass unmodified

## 2. Code-to-HTTP mapping

- [x] 2.1 Implement the total mapping from each `TaskStatus` member to one HTTP status, in a shared function under `src/common/core/utils/`, with a `default` arm mapping an unnamed member to the internal-failure status; verify every declared member maps, the mapping is injective on the declared set, and an arbitrary value the schema does not name yields the internal-failure status
- [x] 2.2 Add unit tests asserting the retryability intent: malformed request, not found, and unimplemented map to non-retryable statuses, while timeout, capacity, and temporary unavailability map to retryable ones; verify each mapping individually in `test/common/core/utils/`
- [x] 2.3 Verify the capacity code maps to 429 and the temporarily-unavailable code maps to 503, each pinned by its own test case so a later change to either is deliberate; verify a capacity refusal and a temporary unavailability produce different HTTP statuses, which the old hardcoded 503 could not express

## 3. `DeliverError` carries a code

- [x] 3.1 Add a status code parameter to `ResultReceiver::DeliverError` in `include/strij/gateway/result_receiver_storage.hh` and update every implementation; verify `bazel build //...` succeeds and the compiler surfaces each unimplemented override
- [x] 3.2 Update the shared mocks in `test/mocks/` to the new signature; verify `bazel test //test/...` compiles and passes
- [x] 3.3 Classify each `DeliverError` call site with the code that matches what it knows: probe deadline → timeout, node disconnected and no-eligible-node and no-capacity → capacity refused, no scheduler claims the type → malformed request, and any site with no specific knowledge → internal; verify each site carries a deliberate code rather than a default
- [x] 3.4 Verify `Deliver`'s signature is unchanged by this change: the code rides inside the serialized `TaskResult` payload, not in a new parameter; verify no `Deliver` overload or parameter was added, confirming the two methods stay distinct (design D4a)

## 4. Gateway routes a failed result as an error

- [x] 4.1 In `GatewayTlvHandler`'s `kResult` case, branch on the success predicate: call `Deliver` for success and `DeliverError(code, body)` for a non-OK status; verify a failed result never reaches `Deliver` as a result payload
- [x] 4.2 Remove the receiver on a failed result regardless of `is_final`, since a failed task produces no further results; verify a failed intermediate result does not leave the receiver registered
- [x] 4.3 Add tests for a failed first result, a failed non-final result, and a result whose status is a value the schema does not name; verify each in the gateway TLV handler test

## 5. HTTP status reflects the failure

- [x] 5.1 In `HttpResponseFramer`, write the mapped HTTP status instead of the hardcoded `200 OK` on the success path and the hardcoded `503` on the error path; verify a successful first result still writes 200 with `Content-Length` framing
- [x] 5.2 Report a failure only on the first result: when a failure arrives after the response has begun, leave the written status unchanged and log it as unexpected, naming the task and the code; verify the status line is untouched and the log is emitted
- [x] 5.3 Verify an unrecognized code yields the internal-failure HTTP status and never 200; verify as a test case in `test/gateway/core/`
- [x] 5.4 Verify the existing chunked-streaming scenarios still pass unchanged, confirming the first-result-only rule did not alter successful streaming; run `bazel test //test/gateway/...`

## 6. Producers set codes

- [x] 6.1 Deferred with cross-reference, acknowledged at verify on 2026-10-08: creating the openworkflow handler is owned by `openworkflow-call-strij` (its task 6.7), which lands second and writes the handler against the final `DeliverError(reason, code)` contract rather than amending it afterwards. Not completed in this change by design.
- [x] 6.2 Verify the two-hop child path: a child's success payload crosses the wire as an unmodified `TaskResult` (pinned by `NodeConnectionResultReceiverTest.DeliverWritesFinalResultFrame` and `DeliverWritesNonFinalResultFrameExplicitly`), but a child's failure does **not** carry the code upstream — `NodeConnectionResultReceiver::DeliverError` writes reason-only `kTaskRejected` by design D4a/D5 (pinned by `child-result-routing` and `NodeConnectionResultReceiverTest.DeliverErrorWritesRejectedFrame`), and the submitting node's default local scheduler classifies the rejection from the reason (`TASK_STATUS_CAPACITY_REFUSED`). The earlier escalation is resolved: on 2026-10-08 the proposal was amended to state the two-hop failure path does not carry the code; the spec is unchanged.
- [x] 6.3 Verify an admission refusal that a gateway scheduler recovers from is never reported to the client, and an unrecovered one is reported with a code reflecting the refusal; verify both in the gateway scheduler tests

## 7. Integration and verification

- [x] 7.1 Add an end-to-end test asserting two failures with different causes produce two different HTTP statuses, which is the behavior the hardcoded 503 made impossible; verify in `test/gateway/`
- [x] 7.2 Run `make test` and `make check`; verify all pass
- [x] 7.3 Update `AGENTS.md` with the `status` field, the code set, the first-result-only rule, and the `TaskRejected`/`TaskDecline` non-participation; verify `make check_namespaces` and `make check_includes` still pass
