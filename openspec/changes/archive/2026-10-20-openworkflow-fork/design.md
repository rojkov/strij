# Design

## Context

See `proposal.md` — Why. Constraints that shape the approach, all inherited from the landed interpreter design (`openspec/changes/archive/2026-10-16-openworkflow-call-strij/design.md`, D1–D8; this change's decisions are numbered fresh):

**`Submit` is not a barrier.** A local child can complete inside `Submit` (reentrancy flag `in_submit_` + `pending_` slot today); a forwarded child resolves later over TLV. Any number of these can now be in flight at once, and outcomes for one branch can land while another branch is inside `Submit`.

**Abandonment is free; cancellation does not exist.** `ChildTaskSubmitter` has no cancel. Child receivers already hold a `weak_ptr` to the interpreter, and `OnChildOutcome` already drops outcomes for an unmatched id — "abandon a branch" is exactly "stop matching its child ids," with no new machinery.

**Every child failure arrives as `DeliverError`** at the receiver, carrying reason and status (the local sender and the gateway both convert non-OK results before the receiver sees them). Fork's failure policy therefore needs only the two outcome kinds the receiver already reports.

**The validation/execution split (D7 of the prior design) is load-bearing.** Everything derivable from the document — call targets, argument keys, requirement kinds, expression syntax, branch structure — must fail before the first child is submitted. Fork adds one purely static fact: a competing fork with no branches can never resolve.

**The parser already models fork.** `ForkBody { Tasks branches_; bool compete_; }`; each branch is a named task, which may be a call, a `do`, or a nested fork. No SDK change is needed or made.

## Goals / Non-Goals

**Goals:**

- Replace the flat step list with an execution tree that runs fork branches concurrently and composes under nesting.
- Fork join and failure semantics exactly as specified in the `openworkflow-interpreter` delta.
- Leave seams so the remaining task kinds (`for`, `try`, `switch`, `set`, `wait`, `listen`, `run`, `emit`, `raise`) slot in as new frame or step kinds without reworking the loop, the propagation path, or the wait model.

**Non-Goals:**

- `for`, `try`, `switch`, `set`, `export`, `wait`, `listen`, `run`, `emit`, `raise` — all remain rejected by validation as today.
- Cancelling abandoned children. Abandoned branches keep running to completion; their outcomes are ignored. A cancel channel is a separate change if ever needed.
- Multi-resumption waits (`listen` receives many events over one subscription). Nothing in this change needs it; forcing the wait abstraction to be multi-shot now would dwarf the work — the same argument D2 of the prior design used to reject coroutines.
- Fairness or deterministic interleaving guarantees across branches beyond the advancement rule in D3.
- Any change to `ChildTaskSubmitter`, the scheduler/router contracts, wire protocols, or configs.

## Decisions

### D1 — Sequence frames and fork frames replace `CollectSteps` and the flat list

Execution state becomes a tree of frames. A *sequence frame* owns an ordered list of tasks, a cursor, and its own data value; a *fork frame* owns the fork's branches (one child sequence frame each), the compete flag, and the join state. The root of the tree is a sequence frame over the document's top-level `do`. A nested `do` is a child sequence frame; a fork is a child fork frame. The interpreter advances frames explicitly — no coroutines, consistent with the prior design's D2.

```
  document                          frame tree
  +---------------------------+     +----------------------+
  | do:                       |     | Seq[root]  cursor-->1|
  |   a: call                 | --> |  +- call a           |
  |   raiseAlarm: fork        |     |  +- Fork[compete]    |
  |     compete: true         |     |     +- Seq[br1] wait-->child7
  |     branches:             |     |     +- Seq[br2] wait-->child8
  |       callNurse: call ... |     |     +- Seq[br3] done   |
  |       callDoctor: call ...|     |  +- call d            |
  |   d: call                 |     +----------------------+
  +---------------------------+
```

An outcome resumes the frame that owns the matching child id: the owning sequence frame applies the outcome to its data value and advances its cursor; when a fork frame's join condition is satisfied it writes the join value into its *parent* sequence frame's data value and releases the parent's cursor.

**Alternatives rejected:** (a) *nested interpreters* — spawn a sub-interpreter per branch. Reuses machinery but composes two mechanisms under nesting (fork-in-fork means interpreter-in-interpreter-in-...), duplicates the reentrancy logic at every level, and forces a parent-facing completion API that the frame walk already is. (b) *flattening with a fork-aware index encoding* — keeps the vector, encodes branch membership alongside. Rejected by the premise: it cannot express concurrency cleanly and dies again at `for`/`try`.

### D2 — Branch advancement runs each branch until it suspends, in declaration order

Whenever the fork frame is (re)activated — at fork entry or on a branch outcome — it advances each incomplete branch in declaration order until that branch suspends awaiting a child or completes. This is a policy of convenience, not a fairness guarantee: it makes the submission order deterministic for tests (branch 1's children are submitted before branch 2's, which matters because a local child can resolve inside its own `Submit` and must not disturb a sibling's advancement).

### D3 — Join and failure semantics

The join value: `compete: false` → JSON array of branch results in declaration order; `compete: true` → the first terminal branch result. Each branch's chain seeds from a copy of the data value at the fork and flows through the branch's own steps; a nested fork's join is simply its branch's contributed result.

Failure is one rule in both modes: **the first terminal branch failure ends the workflow immediately**, carrying the failure's status, with the branch named in the message; siblings are abandoned. Under `compete: true` the first terminal outcome — success or failure — decides the fork. Under join-all, a non-JSON branch result fails at the join site (naming the branch) rather than being encoded — a join that has all results in hand but cannot build the promised JSON array is a run failure, not a silent coercion.

Empty competing forks (`branches: []`, `compete: true`) cannot resolve by construction and are rejected in the validation pass as malformed; an empty join-all fork is well-defined (`[]`).

### D4 — The reentrancy mechanism generalizes from one slot to a keyed map

The prior design's `in_submit_` flag survives unchanged in purpose, but its payload changes shape: `pending_` becomes a small map keyed by child id. An outcome arriving while *any* branch is inside `Submit` is recorded there; after `Submit` returns, the advancing branch drains its own entry before continuing. Per-branch waiting state lives in the sequence frames; the fork frame tracks the set of child ids its branches are awaiting only insofar as outcomes route by owner (see D5 — routing by child id through an owner map is sufficient).

### D5 — Outcome routing and failure propagation both walk the frame tree

Two upward walks, deliberately separate:

*Outcome routing.* A child outcome arrives with a child id. An owner map (child id → owning sequence frame) routes it. The owner applies it (value replace or failure). A failure escalates by the second walk; a success may satisfy a parent fork frame's join condition, which releases the parent sequence frame.

*Failure propagation.* A failure walks from the failing frame toward the root; **each frame on the path may intercept it**. Today no frame intercepts, so every failure reaches the root and fails the workflow — behavior identical to the flat model. `try` later implements interception as a frame kind; no failure site is re-plumbed when it does. This is the seam that keeps `try` an increment rather than a rework.

### D6 — The step contract allows completion without a child

A step's execution has two possible outcomes: it registered a wait (today: always a child submission), or it completed inline with a value. Inline completion is the degenerate case the existing `pending_` drain already handles for local children; the step loop just no longer assumes submission is the only way to reach it. This costs nothing now and is the slot `set`, `emit`, and `raise` fill later.

### D7 — Validation walks the frame tree's shape, statically

`ValidateDocument` no longer takes a flat vector; it recurses the document structure (the same walk that builds frames), applying the existing checks — call targets, argument keys, requirement kinds, expression syntax — at every task node including branch tasks. The one new static check is the empty competing fork (D3). No data-dependent check moves: branch results, JSONness of joins, and expression subjects all fail at execution time as before.

## Risks / Trade-offs

**[A fork submits N children at once; a node can over-commit]** → Accepted: this is what parallelism means, and admission control (`AdmissionController`) already gates what the node admits. Fork does not bypass it — each branch child is an ordinary submission.

**[Abandoned branches keep running]** → Accepted per D3's semantics (settled explicitly): losers and siblings of a failed branch run to completion unseen. Their receivers are weak; late outcomes are dropped. If wasted work ever matters, a cancel channel is a separate change.

**[Outcome routing needs an owner map that must stay consistent as frames complete]** → The map is mutated only at submit (register) and terminal outcome (erase), both on the dispatcher thread; frames outlive their map entries' use by construction (the entry is erased before the frame is popped). Unit-tested with fork + local-admits-inline sequences.

**[Interleaving policy (D2) accidentally becomes observable semantics]** → The spec does not require order; only tests observe it. A future change may alter advancement order without a spec delta.

**[The frame tree reworks the file every future task kind touches]** → That is the point: the rework happens once, now, under fork's pressure — which is the mildest of the control-flow kinds — instead of once per kind.

## Migration Plan

No breaking change: fork previously failed validation as `UNIMPLEMENTED` and now executes; every previously-valid document behaves identically. The rework is confined to `interpreter.{hh,cc}` (plus tests). Rollback is reverting the change; no config, protocol, or artifact format is involved.

## Open Questions

- **Branch-result naming in failure messages.** The spec requires naming the branch; the exact format (`branch 'callNurse': ...`) is left to implementation, matching the existing `step 'x': ...` convention.
- **A per-fork concurrency cap.** If a document forks wide (dozens of branches) on a small node, admission refusals surface as ordinary branch failures and fail the workflow. A future DSL `parallelism` hint or interpreter-side cap would change the spec and is deferred until real documents need it.
