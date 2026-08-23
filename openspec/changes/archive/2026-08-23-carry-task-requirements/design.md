## Context

Today the gateway resolves task hardware requirements from `resources.*` parameters (`ParamsOnlyRequirementsResolver`, `gateway_http_handler.cc:95-98`) and uses them only for routing (`Scheduler::Choose`) and shadow accounting (`ExactStateTracker::RecordSubmission`) — the resolved value is then discarded when the `Task` frame is serialized. The nodeagent independently re-derives requirements from operator-declared `default_resources` in its own capabilities advertisement (`nodeagent_tlv_handler.cc:34-42`) for admission. The two sources never reconcile, so routing decisions, gateway accounting, and node admission can operate on three different numbers.

This change was explored alongside (and is a prerequisite for) a future pluggable local-scheduler extension on the nodeagent, which wants pre-resolved requirements handed to it. The user has decided: resolve before route; drop node-side fallback entirely; no migration shims needed (no production clusters); treat an absent wire field as empty requirements.

## Goals / Non-Goals

**Goals:**

- Make the carried `Task.requirements` the single source of truth consumed by node admission, gateway shadow accounting, and (future) scheduler extensions and probes.
- Delete the node-side resolution path (`resolveRequirements`, `default_resources`) — nodes trust the wire value.
- Keep resolution strictly pre-route so the ordering survives replacing `Choose()` with scheduler-owned fire-and-forget `Schedule()` later.
- Preserve all existing capacity enforcement: pools, reservations, per-type concurrency, pool-total capping of declared requirements.

**Non-Goals:**

- No function-plane resolver implementation (the `RequirementsResolver` seam stays as-is with `ParamsOnlyRequirementsResolver` as v1).
- No `capability_version` bump or version-negotiation discipline (explicitly deferred).
- No machine-readable rejection-reason taxonomy; no queueing/buffering behavior on the nodeagent.
- No changes to `NodeState` reporting semantics.

## Decisions

### D1: Requirements travel inside the Task message

`strij.task.Task` gains `ResourceRequirements requirements = 5`. Alternatives considered:

- *Separate TLV frame* (e.g. a submission envelope pairing task + requirements): rejected — two frames introduce ordering/coupling questions and duplicate what proto evolution already gives us for free.
- *Keep encoding in `parameters` map*: rejected — keeps parsing at the node and leaves the stringly-typed ambiguity (`resources.` vs `resources-` prefix) in the admission path.

Message fields have presence (`has_requirements()`) even in proto3, so absence is representable. The field is plain `ResourceRequirements`, not `optional`-wrapped — presence comes from message semantics alone.

### D2: Absent field ⇒ empty requirements (permissive)

A `Task` without `requirements` is admitted against empty requirements (concurrency limits still apply). Rationale: identical to today's behavior for an undeclared type, keeps handcrafted/direct TLV submissions workable, and the strij gateway always sets the field anyway. Alternative (reject absent-field tasks) was considered for a strict "no unaccounted admissions" invariant and dropped as unnecessary friction given zero deployed fleets. Note the related proto3 limitation recorded here: maps cannot distinguish explicitly-empty from unset, but since both mean "no pool accounting", the distinction is immaterial.

### D3: Resolve-before-route, parameter-driven only

Resolution happens once in `GatewayHttpHandler` before scheduling; because defaults are deleted (D4), nothing cluster-dependent must be known at resolve time — no chicken-and-egg with per-node defaults. Route-then-finalize (merge chosen node's defaults after `Choose()`) was rejected: it couples resolution to the choice call site that will disappear when schedulers own sending. The resolver output is set on the `Task` immediately after parameters are populated; `Choose()`/`RecordSubmission()` consume that same object so there is exactly one resolved instance in flight per task.

### D4: Node-side defaults are removed, not deprecated

`default_resources` fields are deleted from `HandlerCapability` and `HandlerCapacity`; `resolveRequirements()` is deleted from `NodeagentTlvHandler`; `capabilities.cc` stops copying the field; `TaskHandlerFactory::ParseConfig` returns concurrency only. Config YAMLs rejecting an unknown `capacity.default_resources` key is acceptable (no production configs exist). This also closes ROADMAP.md's open question about `default_resources` × repo-resolved-requirements interaction.

### D5: Admission consumes the carried value verbatim

`HandleFrame` calls `Admit(task.type(), task.requirements())`. Trust model shift made explicit: the node believes the wire value rather than enforcing its own defaults. Exposure is unchanged in practice — clients already influence routing via the parameters path today, and `Admit` still caps requests against advertised pool totals, so a lying gateway over-admits only within advertised capacity.

## Risks / Trade-offs

- [Gateway bug sends wrong requirements → node over/under-admits silently] → Mitigation: single construction site (`gateway_http_handler.cc`), one shared object feeding Choose/RecordSubmission/frame; nodeagent tests assert reservation equals carried value (see specs).
- [Foreign submitters bypass pool accounting by omitting the field] → Accepted (D2); matches today's undeclared-type semantics. Revisit if direct-TLV submitters become a real deployment pattern.
- [Operator muscle memory: declaring `default_resources` per handler stops working] → Mitigation: update `docs/config.md` and both example YAMLs in the same change; config loader rejects the stale key loudly rather than ignoring it.
- [Empty-requirements tasks consume unbounded concurrency] → Pre-existing property of undeclared types; unchanged. Handler `concurrency` remains the lever.

## Migration Plan

Single-step swap, no compat layer: protos, gateway, nodeagent, examples, docs, and tests land together. Rollback = revert the commit set. No data on disk depends on either schema.

## Open Questions

(None remaining — resolve-before-route, treat-as-empty, and defaults removal were settled during exploration.)
