# Design

## Context

See `proposal.md` — Why, and `specs/openworkflow-interpreter/spec.md` for the behavior contract. The constraints that shape the approach are inherited from the landed frame design (`openspec/changes/archive/2026-10-20-openworkflow-fork/design.md`) and the task pipeline (`openspec/changes/archive/2026-10-21-openworkflow-task-data-shaping/design.md`):

**The parser and SDK already model `for`.** `ForSpec { optional each, optional at, variant<string, Value> in }`, `ForBody { ForSpec for_, optional while, Tasks do_ }`, and `TaskBody` already include it (`src/openworkflow/task.hh:87`). No parser or SDK change is made.

**The frame tree is the execution model.** `SequenceFrame` owns a cursor and a data value; `ForkFrame` owns concurrent branch sequences and a join. A composite task materializes its child frame lazily when its step is entered. `for` is a sequential composite and fits the same shape.

**The data chain is the expression subject.** A composite's transformed input seeds its child, and the child's final value becomes the composite's raw output, which then flows through `output.as` / `export.as` / `then`.

**Expression variables are fixed today.** `ExpressionArguments` binds `$context`, plus `$input`/`$output` for shaped/exported slots (`expression.hh:27`). `jq_compile_args` receives bare variable names; expressions reference them with a `$`. `for` is the first construct that introduces additional lexical variables, so both validation and evaluation need a local-variable channel.

**The validation/execution split is load-bearing.** Everything derivable from the document must fail before the first child is submitted: expression syntax, `in`/`while` wrapping, loop-variable names, and every task in every nested body.

## Goals / Non-Goals

**Goals:**

- Execute `for` on the existing frame tree without flattening or a nested interpreter.
- Give loop variables a lexical scope that survives suspension and composes with nesting and forks.
- Keep validation's notion of scope identical to the runtime's.
- Preserve the ordinary step pipeline around the loop (`if`, `input.from`, `output.as`, `export`, `then`, `end`).
- Bound synchronous iteration so a huge non-suspending loop cannot starve the event loop.

**Non-Goals:**

- `$input` binding in `for.in` or other task-definition slots beyond what this repo already binds.
- Streaming the collection without materializing the array.
- Cancellation of an abandoned loop; abandoned children keep running and their outcomes are dropped, as with forks.
- Loose expression mode or new runtime-expression arguments.
- Any change to `ChildTaskSubmitter`, `ResultReceiver`, the wire protocol, configs, or the handler entrypoint.

## Decisions

### D1 — `for` is a sequential `ForFrame`, not an inline `SequenceFrame` loop

A `ForFrame` owns:

- a pointer to the `NamedTask` that declared it (for the name and `ForBody`),
- the transformed input received on `Enter`,
- the collection as a JSON array, the next index, and the accumulator,
- at most one body `SequenceFrame` (the current iteration),
- the lexical scope of its enclosing sequence,
- a per-stretch iteration counter.

`SequenceFrame::Advance` adds `ForBody` to the container arm: `advanceContainer` materializes the `ForFrame`, enters it with the transformed input, and advances it like a `do` or `fork`. A completed `ForFrame` returns the accumulator, which then flows through the parent's `output.as`/`export.as`/`then` pipeline unchanged.

```
SequenceFrame (cursor on "loop")
  +- ForFrame
       +- SequenceFrame[body, iteration 0]
       +- SequenceFrame[body, iteration 1]
       +- ...
```

**Alternatives rejected:** (a) a loop state machine inside `SequenceFrame` — it would add collection, index, scope, and `while` state to the generic sequence and complicate the cursor contract; (b) a nested interpreter — duplicates the drive/reentrancy logic under nesting, exactly the alternative the fork design rejected.

### D2 — Lexical variables live on a frame-owned scope chain

Add a shared immutable scope node:

```cpp
struct LexicalScope {
  std::shared_ptr<const LexicalScope> parent;
  std::string each_name;   // without '$'
  utils::Jv item;
  std::string at_name;     // without '$'
  utils::Jv index;
};
```

`SequenceFrame` carries `std::shared_ptr<const LexicalScope> scope_`. `do`, `fork`, and `for` frames created by that sequence inherit the pointer; a `for` body sequence instead receives a new node whose parent is the `ForFrame`'s scope. Nested loops therefore inherit outer bindings, and a reused name shadows the outer one because resolution walks from the innermost node outward.

The scope must live on the frame, not on an ambient interpreter stack: a suspended body sequence evaluates its own `output.as`/`export.as` from `OnChildOutcome`, outside any `ForFrame::Advance` call. Holding the scope on the sequence keeps the item/index alive until the iteration completes, including through that resumption path.

**Alternatives rejected:** writing loop variables into `$context` — it would violate the settled rule that only `export.as` writes the context, and it would leak across siblings and forks; an interpreter-side push/pop stack — it would be wrong across concurrent fork branches and fragile across the reentrancy path.

### D3 — Expression evaluation grows a local-variable channel

`ExpressionArguments` gains a lexical-binding view (name plus `const utils::Jv*`), resolved from the scope chain by the frame before calling the interpreter. Validation takes the same names without values. `ExpressionEngine` appends locals after the reserved variables when it compiles and when it builds the `jq_compile_args` argument array. Locals are deduplicated innermost-first so shadowing is unambiguous.

Two typed slots join `TypedSlot`:

- `kForIn` for the string form of `for.in`;
- `kForWhile` for `for.while`.

Both bind `$context` plus the visible locals; neither binds `$input`/`$output`, matching the repo's narrower binding policy for task-definition expressions. A non-string `for.in` uses the existing literal-capable `Evaluate` path with locals.

Name normalization happens once, before validation and before binding:

1. trim whitespace;
2. default an empty `each` to `item` and an empty `at` to `index`;
3. strip one leading `$` if present;
4. require a valid jq identifier (`[A-Za-z_][A-Za-z0-9_]*`);
5. reject `each == at` and collisions with `context`, `input`, or `output`.

The Go SDK normalizes to `$`-prefixed names because its variable map is `$`-keyed; this repo's binding API takes bare names, so stripping at the boundary is the adapter. Accepting both spellings avoids a gratuitous incompatibility.

### D4 — The collection is evaluated once; iteration uses the accumulator

`ForFrame` evaluates `for.in` on its first `Advance`, against the transformed input:

- a string is compiled as `kForIn` and evaluated as a whole-value `${ }` expression;
- a non-string is evaluated through the literal-capable path (wrapped strings evaluate, bare strings stay literal, inline arrays are the collection);
- the result must be a JSON array or `null`; `null` means zero iterations; any other result fails the step with `INTERNAL` and a message naming the task.

The array is held for the loop's lifetime. Each iteration seeds its body sequence with a copy of the accumulator, where the accumulator starts as the transformed input and is replaced by each completed iteration's value. Zero iterations therefore make the `for` an identity, and the raw output is always the final accumulator.

`while` is evaluated after a completed iteration, against the accumulator, with `$context` and that iteration's scope, before the next body is created. A false result completes the loop with the accumulator; a non-boolean result or a non-JSON accumulator fails the step with `INTERNAL`.

**Alternatives rejected:** re-evaluating `in` per iteration — it would make the collection depend on the accumulator and diverge from the reference; treating a non-array as a single item (the Go SDK's `reflect` fallback) — the DSL types `in` as a collection, and this repo prefers an explicit failure over an accidental one-element iteration.

### D5 — `ForFrame::Advance` is a resumable, lazy state machine

```
Enter(data):
  accumulator = data; started = false

Advance(context):
  if !started:
      items = eval(for.in, accumulator, outer_scope)   // once
      started = true
  reset iteration budget
  loop:
      if body child exists:
          r = body.Advance(context)
          Suspended  -> Suspended
          Failed     -> keep child (origin pointer), mark failed, Failed
          Ended      -> mark completed, Ended(propagated)
          Completed  -> accumulator = r.value
                        if while and !eval(while, accumulator, iteration_scope):
                            destroy body; mark completed; return Completed(accumulator)
                        destroy body
          if no more items: mark completed; return Completed(accumulator)
      else if next_index < items.size:
          create body SequenceFrame(body.do_, iteration_scope)
          body.Enter(accumulator.Copy())
          ++next_index
      else:
          mark completed; return Completed(accumulator)
```

The child is created lazily and destroyed after its iteration, so at most one iteration body is live. On failure the child is kept alive because `ExecutionFailure::origin_` may point into it, mirroring `SequenceFrame::advanceContainer`.

A local child may complete inside `Submit`; the body sequence already handles that, so `ForFrame` only observes `Completed`, `Suspended`, `Ended`, or `Failed` from `body.Advance`.

`Abandon` abandons the current body and marks the loop abandoned; the body forgets its waiting child, so late outcomes are dropped.

### D6 — The ordinary step pipeline wraps the loop

`ForFrame` returns the accumulator as a container result. The parent `SequenceFrame` then applies:

- the `for` task's `output.as` against the accumulator with `$input`/`$output`,
- its `export.as` against the shaped output,
- its `then` directive in the parent scope.

`if` is evaluated by the parent before the loop starts, so a false condition skips `for.in`, every iteration, and the shaping pipeline. `input.from` seeds the transformed input that becomes the initial accumulator and the `for.in` subject.

Inside the body, flow directives resolve against the body's own sequence scope. `exit` completes that iteration's body and lets the loop advance; `end` returns `Ended`, which the loop propagates so no enclosing `do`, `for`, or `fork` applies its shaping. A body `export` is allowed outside a fork branch and updates the context per iteration; inside a fork branch it remains rejected.

### D7 — Validation mirrors the runtime scope tree

Validation gains a local-name stack parallel to `D2`:

- the `for` task's own `if`/`input.from`/`output.as`/`export.as` see only enclosing loop names;
- `for.in` sees only enclosing names (its own variables do not exist yet);
- `while` and every body task see the enclosing names plus the loop's own names;
- nested `do`/`fork`/`for` propagate the accumulated names;
- each `for` body is validated as its own sequence scope, including duplicate-name and flow-target resolution;
- a `for` inside a fork branch passes `inside_fork_branch` into its body so the existing export rejection applies.

New static checks, all reported before the first child is submitted:

- `for.in` string form is a whole `${ }` expression and compiles;
- every expression in a non-string `for.in` compiles and the inline value is an array;
- `for.while` is a whole `${ }` expression and compiles;
- loop variable names normalize, are valid, differ, and do not shadow reserved names;
- every body task passes the existing validation with the loop names visible.

### D8 — Synchronous iteration is bounded per stretch

`SequenceFrame` already bounds synchronous transitions because `then` may jump backward. A `for` over a large array can likewise run many iterations without suspending even though each iteration resets the body sequence's own budget. `ForFrame` therefore counts iterations advanced in one `Advance` call against the same generous implementation-defined budget, resetting when it resumes after a child. Exceeding it fails the workflow with `INTERNAL`, naming the `for` task. A loop that suspends on a child is unaffected.

**Alternative rejected:** relying only on the body sequence's budget — the body resets on every iteration, so a large synchronous collection would never trip it.

### D9 — Failure attribution

- `for.in`/`while` failures are local to the `for` step: `status = INTERNAL`, `step_ = for task name`, `origin_ = nullptr`. A top-level failure names the step; a fork composes the branch name and then the step name.
- A body failure propagates unchanged, keeping the inner body's origin and step. The loop does not re-wrap it; the existing fork composition and top-level naming remain the single formatting path.
- A non-array collection, non-boolean `while`, and non-JSON subject are data-dependent and therefore run failures, never validation failures.

## Risks / Trade-offs

- **Loop-variable plumbing touches every expression path** → Keep one `LexicalScope` helper used by every `FrameContext` method, pass names through validation in one parallel stack, and cover every slot (`if`, `input.from`, `set`, call `with`, `switch.when`, `output.as`, `export.as`, `for.in`, `for.while`) with a loop-variable test.
- **Ambient-scope implementations can fail across suspension or concurrent branches** → Scope is owned by the frame that evaluates the expression; no interpreter-global current scope.
- **Invalid or duplicate variable names can make jq compilation ambiguous** → Normalize and reject invalid names, `each == at`, and reserved collisions in the validation pass; deduplicate innermost-first at compile/run.
- **Non-array collections diverge from the Go SDK's single-item fallback** → Explicit failure is the deliberate choice; the CTK only exercises arrays, so no observable conformance loss.
- **`$input` is not bound in `for.in`** → This repo already binds `$input` only for `output.as`/`export.as`; nested-loop documents use `.` for the current subject or `$context` for a preserved collection. This is documented as a local narrowing, not a new binding.
- **A large synchronous `for` can still do real work before the bound trips** → The bound is generous but finite; a run-wide cap or timeout remains a separate concern, as with synchronous `then` loops.
- **Body export inside a loop can be surprising** → It is explicitly allowed outside a fork branch and rejected inside one, matching the existing concurrency rationale.
- **Keeping the failed body alive until the run ends** → Small and bounded by one iteration body; it preserves `origin_` validity exactly as the existing container path does.

## Migration Plan

No wire, config, proto, storage, or artifact-format migration. `for` previously failed validation as `UNIMPLEMENTED`; it now executes. Documents that use bare `in`/`while` were already rejected by strict mode once execution existed, so the strict spelling is the only migration note for authors.

Implement in dependency order, keeping the suite green:

1. `expression.{hh,cc}` — local-variable arguments, `kForIn`/`kForWhile`, name handling.
2. `frame.{hh,cc}` — `LexicalScope`, scope ownership on sequences, `ForFrame`, `FrameContext` effects.
3. `interpreter.{hh,cc}` — collection/`while` evaluation, scope-aware validation, name checks.
4. Frame/interpreter test fakes — new `FrameContext` methods and scope arguments.
5. Tests and `docs/testing-openworkflow.md` examples.

Rollback is reverting the change; no external state is involved.

## Open Questions

- The exact synchronous iteration constant and the wording of the bound failure are implementation details.
- Whether an iteration index is included in body failure messages is left to implementation; the spec only requires the failing step to be named.
