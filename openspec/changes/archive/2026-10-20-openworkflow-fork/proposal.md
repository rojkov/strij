# Proposal

## Why

The Open Workflow interpreter executes only `do` (structural) and `call` steps over a flat, pre-flattened step list (`CollectSteps`). The DSL's `fork` task — parallel branches, optionally competing — cannot be expressed in that model: it needs concurrent cursors, multiple outstanding children, and a join that defines the value continuing the data chain. More fundamentally, flattening breaks down for every remaining control-flow task kind (`for`, `try`, `switch`), so supporting fork by patching the flat list would guarantee a second refactor later.

## What Changes

- The interpreter executes the document as a **tree of ordered sequences** instead of a flat step list: nested `do` bodies and `fork` branches are nested sequences; a `fork` step suspends its enclosing sequence until its join resolves. `CollectSteps` is removed.
- **`fork` becomes executable.** Each branch is a full sequence seeded with a copy of the fork's current data value; its own steps flow through its own chain. Branches may contain `call`, `do`, and nested `fork`; every other task kind remains `UNIMPLEMENTED` as today.
- **Join semantics.** `compete: false` (default): the join value is a JSON array of all branch results in declaration order. `compete: true`: the join value is the first terminal branch result; losing branches are abandoned and their late outcomes ignored (no child-cancel channel is needed or added).
- **Failure policy (both modes): fail fast.** The first terminal branch failure fails the whole workflow with that failure's status, naming the branch; siblings are abandoned. An opaque (non-JSON) branch result under join-all fails the workflow at the join site, naming the branch.
- **Validation extends to branches.** The data-independent validation pass recurses into fork branches (call targets, argument keys, requirement kinds, expression syntax). A `fork` with `compete: true` and no branches is rejected in the validation pass as malformed.
- **Substrate seams for future task kinds** (no behavior change now): steps may complete inline without submitting a child; failure propagation travels upward through sequence frames so a future `try` can intercept it; wait state is per-sequence rather than a single global slot. Multi-resumption waits (`listen`) are an explicit non-goal of this change.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `openworkflow-interpreter`: execution model generalizes from a flat step sequence to a tree of sequences; fork join, compete, failure, and validation requirements added; data-chain requirement extended with per-branch chains and the join value.

## Impact

- `src/nodeagent/extensions/task_handlers/openworkflow/interpreter.{hh,cc}` — the state machine is reworked (frames replace `steps_`/`step_index_`; `pending_` becomes keyed by child id); `CollectSteps` and `ValidateDocument`'s flat-vector signature go away.
- Tests: `test/nodeagent/extensions/task_handlers/openworkflow/interpreter_test.cc`, `openworkflow_e2e_test.cc` gain fork coverage; existing sequential tests must keep passing unchanged in behavior.
- No changes to the SDK (`src/openworkflow/` — the parser already models `ForkBody`), `ChildTaskSubmitter`, the scheduler/router contracts, wire protocols, or configs.
- No breaking change for existing documents: fork previously failed validation as `UNIMPLEMENTED`; it now executes. All other behavior is unchanged.
