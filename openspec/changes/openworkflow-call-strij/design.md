# Design

## Context

See `proposal.md` — Why. The constraints that shape the approach:

**A child's result may arrive before or after the submission call returns.** `ChildTaskSubmitter::Submit` is fire-and-forget and takes receiver ownership. Locally, `DefaultLocalScheduler::Schedule` can run the child to completion *inside* `Submit` — `WorkflowChildReceiver`'s own comment in the existing reference handler says Deliver/DeliverError may run before `Submit()` returns. Forwarded children resolve later, over TLV, via the gateway. So `Submit` is not a barrier and the interpreter cannot use the call stack to hold per-step state.

**A child reports its outcome through `ResultReceiver`'s two methods, and both are reachable.** `Deliver(span<byte>, is_final)` for a result, `DeliverError(string_view, task::TaskStatus)` for a failure. `TaskResult.status` now exists, but a child's failure still arrives as `DeliverError` at the receiver: both the local sender (`StorageResultSender::Send`) and the gateway's result handler inspect `status` and convert a non-OK result into `DeliverError` before it reaches the submitter's receiver. The interpreter therefore handles two outcomes at every step, not one.

**The evaluator contract already separates compile from run.** `EvaluatorFactory::Compile(source, variable_names, deps) -> StatusOr<EvaluatorPtr>`; `Evaluator::Run(Jv input, span<Jv> args) -> StatusOr<vector<Jv>>`. Compile validates the program; only Run needs data. This is the lever behind the static/dynamic split.

**`openworkflow::Value` is signed and JSON-faithful** (`variant<monostate, bool, int64_t, double, string, Array, Object>`), while `ResourceRequirements.resources` is `map<string, uint64_t>`. The int64→uint64 conversion is new and has no precedent in the repo.

**`utils::Jv` is internal to `src/` and is the only JSON boundary.** It wraps RAII `jv` and exposes `Parse`, `Dump`, `IsValid`, `Copy`. `Value` has no JSON representation today.

**`Input::from_` is `variant<string, Value>`, and neither arm marks expressions.** The parser maps a YAML scalar to the string arm and a non-scalar to the `Value` arm; `decodeValue` then recurses through maps and sequences, mapping scalars to `bool`/`int64_t`/`double`/`std::string`. Nothing records which strings are expressions — the settled parser spec requires preservation "with no … interpretation". Expression recognition is therefore entirely the interpreter's, and strict mode is defined in terms of the string *characters* (see D5).

## Goals / Non-Goals

**Goals:**

- A step loop that survives a `Submit` that resolves before it returns, without deadlocking and without double-advancing.
- A validation pass that is complete for data-independent facts and cannot be fooled into claiming more than it checks.
- One coercion table for every value→child-task conversion, so the same mistake is never reported two ways.
- Keep the SDK limited to the parser, the `Value` tree, and the JSON bridge, with the interpreter beside its handler in nodeagent.

**Non-Goals:**

- `use.functions` dispatch, and the other nine task kinds.
- Explicit `with.body: json` declaration (see Open Questions).
- Any change to `ParamsOnlyRequirementsResolver`, the `Scheduler` contract, `ChildTaskSubmitter`, or `HandleTask`.
- A dry-run mode. The static pass is a by-product, not a feature.
- Putting any executable artifact in `src/openworkflow/`. That package is a side-less SDK; a node-side component there would claim a cross-cutting namespace it does not own.

## Decisions

### D1 — The interpreter lives beside the handler, not in the SDK

The interpreter lives in `src/nodeagent/extensions/task_handlers/openworkflow/`, in namespace `strij::nodeagent::task_handlers::openworkflow`, and the handler is a thin adapter over it: `HandleTask` parses the body, hands off to the interpreter, and keeps the `ResultSenderPtr` alive. The interpreter reaches children through `ChildTaskSubmitter` from `TaskHandlerDeps`, obtained at factory construction like every other child-submitting handler.

The SDK keeps only what is genuinely SDK-shaped: the parser, the `Value` tree, and the `Value` ⇄ JSON bridge. The parser's own spec frames it as producing a model *"for downstream interpreter consumption"* — the interpreter is a consumer, not part of the published surface.

Two reasons this is the right home rather than a compromise:

- **Namespace coherence.** `src/openworkflow/` has no side, so an executable node-side artifact there cannot declare `strij::nodeagent*` without tripping `check_namespace_coherence.py`, which requires a `/nodeagent/` path to open a nodeagent namespace. The reverse — the interpreter in the handler directory — is simply correct.
- **The SDK stays dependency-light.** The interpreter needs `absl`, `utils::Jv` (internal to `src/`), and the child-submission seam. None of that belongs in a tree whose value is that a third party can parse documents without the runtime.

**Alternative rejected:** placing the interpreter in `src/openworkflow/` to keep it DSL-agnostic, at the cost of a cross-cutting namespace claim and a heavier SDK. The gateway-side pre-validation idea that motivated it (see Open Questions) is a *later* concern and can be met by a small nodeagent-free validator extracted from the interpreter if it ever proves worthwhile — which is cheaper to do on demand than to pay for speculatively.

### D2 — Suspend/resume is explicit: a step index plus a per-step state, not coroutines

The interpreter holds `step_index_` and, when a step is waiting, a `WaitingForChild` state holding the child task id and the position the result belongs to. `Resume(child_id, result)` matches on the id and either advances the index or records a failure.

The local-completes-during-`Submit` case is handled by a **reentrancy flag**, not by a queue: `Submit` is called with `in_submit_ = true`; a receiver that fires during the call records the result into `pending_result_` and returns; after `Submit` returns, the loop drains `pending_result_` before resuming. This is the same pattern the existing reference handler uses, and it is the only ordering that is actually correct — a queue would have to be drained from inside the receiver anyway.

**Alternative rejected:** coroutines (`co_await` on the child). No dependency exists, it would put a C++20/23 runtime requirement on a `#pragma once`-light codebase that targets a specific toolchain, and the state is a single index plus one record — the machinery would dwarf the work.

### D3 — The subject is the data chain, and it is *not* the DSL's `$context`

The reference corpus uses two different things, and conflating them would have been a spec bug:

```yaml
body: '${ . + { status: "sold" } }'      # '.'  = subject = the task-to-task data chain
as:   '$context + { error: $error }'      # '$context' = workflow-scope variable
if:   $context.error == null              #           written only by export/set
```

So the data chain is the jq subject, and `$context` is a separate accumulator that only `export.as` and `set` write. This slice executes neither, so `$context` is bound and permanently empty.

**Alternative rejected:** the "always-JSON context seeded from the data chain" model this design previously used. It was the same runtime behavior but described `$context` as derived from the data, which is false — a document reading `$context` observes `{}`, not the current data. Describing it that way would have made the `set`/`export` tasks, when they arrive, look like they were threading existing state rather than introducing it.

The error site for a non-JSON subject is the one error the subject produces, so there is no separate "context error" case — the earlier two-error-site split (subject vs. empty context) collapses into one, because the empty-context case *is* the opaque-subject case. What survives is the distinction that matters: a subject error names the data, while a syntax error names the expression.

### D4 — Requirements get a strict, hand-written coercion, not the gateway's resolver

The gateway's `ParamsOnlyRequirementsResolver` parses flat `map<string,string>` keys (`resources.` / `resources-` prefixed) with `std::stoull`, whose `pos` it never checks — so `100M` silently becomes `100`, and the `catch` that promises a warning for unparseable amounts never fires. It is gateway-private, opens `strij::gateway`, and cannot be included from nodeagent code without violating `check_namespace_coherence.py`.

The workflow path takes `Value::Object` → `ResourceRequirements` directly:

```
IsInt()                → ok  (AsInt() >= 0)
IsString()             → error "unit suffixes are not accepted"
IsDouble() / other     → error "not an integer"
negative int64         → error "must be non-negative"
int64 > uint64 max     → error "out of range"  (unreachable in practice,
                          checked anyway so the invariant is not implicit)
```

Being *stricter than the gateway* is intentional and is recorded as such. Matching the gateway's lenient behavior would mean two meanings for one pool name depending on how the task arrived, and documents containing `100M` would be unfixable once written.

**Alternative rejected:** extracting the gateway resolver into `src/common/` and sharing it. That is the right fix for the gateway bug, but it is a separate change, and the shared artifact would have to be parameterized by input type anyway (`Value::Object` vs `map<string,string>`) — at which point the sharing is a key-matching loop, not a semantic unit.

### D5 — Strict-mode `${ }` detection is the interpreter's job, scanning recursively

The parser stores no marker. `Input::from_` is `variant<string, Value>`, and `decodeValue` maps a scalar to `std::string` and a map/sequence to a recursive `Value` — with no distinction between the string `"${ .x }"` and the string `".x"` beyond the characters themselves. The settled parser spec requires exactly this ("preserved verbatim as plain strings, with no … interpretation"), so the interpreter does the recognition.

The rule is uniform at every string position: a string is an expression iff it is entirely wrapped in `${ }`, otherwise it is literal text. `evaluate.mode` defaults to `strict`; `loose` is rejected as unsupported in this slice.

The important consequence: **`input.from`'s string arm is NOT automatically a program.** Under strict mode `from: .petId` is the literal text `.petId`. Every `input.from` in the reference corpus is `${ }`-wrapped (`from: ${ .message }`), so the corpus never distinguishes the two readings — which is why an earlier draft of this design got it wrong by treating the string arm as a program.

**Whole-value only, not template interpolation.** `${ }` must span the entire string. `${ "a\(.)b" }` is a single jq program whose result is the whole value, using jq's own interpolation. `"jobs/\(.id)"` is not supported and is rejected with an error saying the wrapper must span the whole value. This is why `with.parameters` is the slot that permits expressions, and why a partially-wrapped parameter is an error rather than a silent literal.

**Alternative rejected:** treating every string in a value tree as a candidate program, falling back to literal on compile failure. That is the `loose` mode, and adopting it as the default would make `from: { path: "/a/b" }` a compile error — a document that reads as a plain literal would fail. It is deferred to the `loose` implementation, where compile-failure-as-literal is the correct fallback *because the author asked for it*.

**Alternative rejected:** having the parser tag expressions with a distinct node type. Cleaner, but it contradicts a settled spec and would require a parser change plus a new delta to `openworkflow-dsl-parser`. Recognizing `${ }` in the interpreter is where the semantics belong anyway, since `evaluate.mode` is a run-time concern.

### D6 — JSONness is sniffed per step boundary, and both branches are represented

The data value is `std::variant<Jv, std::string>`; the Jv branch is only populated when `Jv::Parse` succeeds. After every child result, the bytes are re-sniffed. A non-JSON result moves the chain to the opaque branch, and the next step requiring a JSON subject then fails naming the subject.

**Alternative rejected:** sniffing once at input. A workflow whose first task returns HTML and whose second reads `.field` would either pass validation and fail confusingly, or be rejected at a point far from the cause.

### D7 — Validation compiles every expression before the first submission

`Start()` walks the body, checks call targets, checks the `with` key set, coerces requirements, and calls `Compile` on every string the strict-mode scan (D5) identifies as an expression — discarding the compiled programs, keeping only success/failure. This is cheap (compile is deterministic and the evaluator caches nothing) and it is what lets the failure be reported as "your document is malformed" rather than "your third task failed".

The scan must recurse, so a syntax error inside a nested member is caught here rather than mid-run.

The compiled programs are *not* retained. Retaining them would require an expression cache keyed by source across runs, and the documents are small enough that re-compiling at run time is not the bottleneck. Revisit only if profiling says otherwise.

### D8 — Workflow failure rides the `ResultSender` as a non-OK `TaskResult.status`

The handler's only outbound path is `ResultSender::Send(task::TaskResult)`; it has no `DeliverError` to call. So a workflow failure is reported by building a `TaskResult` whose `status` is a non-OK `TaskStatus` and whose `body` is the message naming the failing step, then calling `Send`. The code travels inside the payload, the same route a successful result takes.

The conversion to an error is the gateway's: the nodeagent serializes the result as a `kResult` frame (`ConnectionResultSender::Send`), and `GatewayTlvHandler::handleTaskResultFrame` sees a non-OK status via `utils::IsTaskResultOk`, calls `DeliverError(body, status)` on the client's receiver, and erases it. So the handler's contract is exactly "send a `TaskResult` with the right status," and the HTTP status follows from `utils::TaskStatusToHttpStatus` at the edge.

Classification is therefore the handler's only new decision. It maps its own failures onto the codes established by the landed `task-result-status` change: a body that will not parse, or a document that fails the pre-submission validation pass, is `TASK_STATUS_MALFORMED_REQUEST`; a failure with no more specific cause is `TASK_STATUS_INTERNAL`; a recognized-but-unsupported call target is `TASK_STATUS_UNIMPLEMENTED`. A failed child's code is propagated as-is.

`RegisterOnClose` is used: if the originating connection tears down mid-run, the interpreter marks itself cancelled and abandons outstanding children rather than resuming into a dead sender.

**Known limitation:** a refused child arrives at the child receiver as `DeliverError(reason, status)`, so both the reason and the code reach the interpreter and can be reported — but a *partial* child result followed by a disconnect is not distinguishable from a refusal, and both are reported as a child failure without distinguishing which.

## Risks / Trade-offs

**[A local child completes inside `Submit`, re-entering `Resume` before the step is marked waiting]** → `in_submit_` flag plus `pending_result_`; the receiver never advances the index itself. Covered by a test that admits a child locally and asserts no deadlock and exactly one step advance.

**[Non-final partial results are mistaken for the final one]** → only the child's terminal outcome (final `Deliver` or `DeliverError`) transitions the step; partial chunks are discarded or buffered, never treated as the value. Needs an explicit test since `Deliver` carries the flag but the child path can send several chunks.

**[`$context` collides with the subject in the reader's mind]** → the two are named differently everywhere (`data chain` vs. `$context`), the interpreter binds `$context` explicitly to an empty object so it is never accidentally unbound, and a scenario pins the distinction. When `export`/`set` land they will introduce a second slot; the interpreter already holds the subject separately, so that is additive rather than a rework.

**[Strict requirements are stricter than the gateway]** → the same document submitted two ways can mean different things. Mitigated by the error wording (which states suffixes are not accepted, not just that parsing failed) and by a note in the design that the gateway divergence is a separate bug.

**[The interpreter grows a `Value` ⇄ `Jv` bridge that later work must live with]** → the bridge is confined to one file with a narrow API (`ValueToJv`, `JvToValue`) and unit tests for round-tripping, so a later change to it is contained.

**[Compile-without-retain does the work twice]** → accepted; expressions are small and run once per step.

## Migration Plan

New handler type (`openworkflow`), no changes to any existing type. Rollback is removing `openworkflow` from the nodeagent's task-handler config and restoring the deleted `workflow` sources.

The existing `"workflow"` reference handler (112 lines, `src/nodeagent/extensions/task_handlers/workflow/`) is **deleted** by this change, not modified — it submits a fixed number of identical children and is not an interpreter, so there is nothing to carry forward. Because the new handler is named `openworkflow`, there is no type collision during the transition, but any node configured with `workflow` loses that task type on upgrade and must be reconfigured. This is the one breaking item and belongs in the release notes.

## Open Questions

- **Whether `with.body: json` is added later** to declare rather than sniff a JSON body. The sniffed model is complete without it; a declaration would only matter for a body that is valid JSON *and* meant to stay a string.
- **A gateway-side pre-validation mode.** A document could be validated before a node is assigned, so a malformed workflow never occupies node capacity. The interpreter is nodeagent-side (D1), so this would mean a second consumer of the SDK's parser and validator — plausible given the static/dynamic split already isolates the data-independent half, but not worth building before the nodeagent path is proven.
- **The `ParamsOnlyRequirementsResolver` truncation** stays a separate change; this design only records that the divergence exists.
- **`evaluate.mode: loose`.** Rejected as unsupported here. When implemented, loose mode means a bare string is attempted as a program and falls back to literal text on *compile* failure — never on run failure, since a program that compiles but errors is a real error the author should see. The interaction worth deciding then: whether a compile-failure fallback should be silent or warn, since silently reinterpreting `"a/b"` as text can mask a typo'd expression.
- **A literal string that starts with `${`.** Under strict mode there is no escape — `"${literal}"` is always an expression. Whether the DSL offers an escape (a doubled `$$`, or a non-`${` quoting form) is a spec question this slice does not answer, and the interpreter should reject the ambiguous case loudly if one is later chosen.
