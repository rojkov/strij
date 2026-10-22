# Design

## Context

See `proposal.md` — Why, and `specs/openworkflow-interpreter/spec.md` for the behavior contract. The constraints that shape the approach are inherited from the landed frame design (`openspec/changes/archive/2026-10-20-openworkflow-fork/design.md`) and the task pipeline (`openspec/changes/archive/2026-10-21-openworkflow-task-data-shaping/design.md`):

**The step contract already allows inline completion.** The fork design introduced the rule that a step may complete without submitting a child; `set` and `switch` are the first users of that seam, so no new frame kinds are needed for either.

**`Submit` is still not a barrier.** A local child may complete inside `Submit`, so a call's continuation can be applied while `ExecuteCall` is on the stack. `then` must not assume that a step completion only happens in the outer `Advance` loop.

**Scopes are not the same as the parsed task list.** `ForkFrame` builds one single-task `SequenceFrame` per branch, while `ForkBody::branches_` is one `Tasks` vector. "Same scope" for a `then` target must follow the runtime shape, not the parser's vector shape.

**Failure propagation already walks upward.** `Ended` must traverse that same path; `exit` does not need a new path because it is just a sequence completing.

**The parser and SDK already model everything.** `SetBody { variant<string, Value> }`, `SwitchBody { vector<NamedCase> }`, `NamedCase { name_, when_, then_ }`, and `Task::then_` exist. No parser or SDK change is made.

## Goals / Non-Goals

**Goals:**

- Execute `set` and `switch` as inline steps with no new frame kinds.
- Make `then` a sequence-level continuation that supports forward and backward jumps, scope exit, and workflow end.
- Keep validation's notion of a scope identical to the runtime's.
- Bound synchronous execution so a legal backward jump cannot wedge the event loop.

**Non-Goals:**

- `for`, `try`, `listen`, `call`'s `then`-like retry behavior, or any other task kind.
- `timeout`, cancellation of looping workflows, or a global run-wide iteration cap.
- Loose expression mode, new runtime-expression arguments, or `$input` binding on the literal-capable path.
- Changing the parser's scalar handling (a scalar `set: 42` remains the string `"42"`).

## Decisions

### D1 — `set` and `switch` are inline step bodies, not frames

`SequenceFrame::Advance` grows two inline arms alongside the existing call/container arms:

```
Advance loop:
  if -> input.from -> body:
      call            -> submit / suspend
      do | fork       -> materialize child, advance
      set             -> EvaluateSet(step, input)          [inline]
      switch          -> SelectSwitchDirective(step, input) [inline]
  raw output -> output.as -> export.as -> directive
```

`set`'s raw output is its evaluated value; `switch`'s raw output is its transformed input. Both then flow through the unchanged `TransformTaskOutput` / `ExportTaskResult` pipeline.

**Alternatives rejected:** a `SetFrame`/`SwitchFrame` — neither has a sequence, a child, or a suspension, so a frame would add ownership and lifetime machinery without buying composability. Deferring `set` until some generic "inline step frame" exists would leave `switch`'s natural targets unavailable and would postpone the first user of the inline seam indefinitely.

### D2 — The interpreter selects switch cases; the sequence resolves the directive

`FrameContext` gains:

```cpp
virtual auto SelectSwitchDirective(const NamedTask& step, const StepDataValue& input)
    -> absl::StatusOr<std::optional<std::string>> PURE;
virtual auto EvaluateSet(const NamedTask& step, const StepDataValue& input)
    -> absl::StatusOr<StepDataValue> PURE;
```

`SelectSwitchDirective` returns the selected case's `then`, or `nullopt` when no case matched. It owns expression evaluation but not scope: it has no access to the sequence's task span. `SequenceFrame` owns the span, so it combines the result with the switch task's own `then` fallback and resolves the name to a cursor index.

Case selection is deferred-default:

1. If any case declares `when`, the input must be JSON.
2. Evaluate conditional cases in declaration order; return the first `true` case's `then`.
3. Otherwise return the remembered no-`when` case's `then`, if any.
4. Otherwise return `nullopt`, and the frame uses the switch task's own `then` (or `continue`).

`when` is evaluated as `TypedSlot::kSwitchWhen` against the transformed input, binds only `$context`, and must yield a boolean, mirroring `if`. A default-only switch never needs a JSON subject.

**Alternatives rejected:** giving the interpreter the scope span — it would duplicate the frame's cursor ownership; returning a resolved index from the interpreter — the interpreter cannot resolve names it does not own.

### D3 — Flow directives are a small value type resolved against a scope

```cpp
enum class FlowKind : std::uint8_t { kContinue, kJump, kExit, kEnd };
struct FlowDirective { FlowKind kind_; std::size_t target_; };

auto ResolveFlowDirective(std::span<const NamedTask> scope, std::string_view then)
    -> absl::StatusOr<FlowDirective>;
```

Resolution is shared by validation and execution, so a document that passes validation cannot fail runtime resolution. `continue`/`exit`/`end` are reserved; every other value must match exactly one task name in the same span. Duplicate names in a scope are rejected up front, making the lookup unambiguous. Self-targets are allowed (loops); the guard in D6 bounds them.

The same resolver is used for:

- a task's own `then`, including `set` and `switch`;
- the selected switch case's `then` (against the switch's containing sequence);
- a switch's own `then` fallback.

### D4 — A completed step records its continuation; `Advance` applies it

The output/export pipeline stays in the two existing completion sinks:

- `SequenceFrame::ApplyOutcome` for a call result (resumed or inline inside `Submit`);
- `SequenceFrame::advanceContainer` for a `do`/`fork` child completing.

After export succeeds, both call one helper that resolves the step's directive:

| Directive | Effect |
|-----------|--------|
| `continue` | `++cursor_` |
| jump | `cursor_ = target_` |
| `exit` | mark this sequence completed; return `Completed{data_}` |
| `end` | return `Ended{data_}` |

Continue/jump update the cursor immediately. `exit`/`end` cannot return a value from inside `ApplyOutcome` (the call may originate from `OnChildOutcome` or from `ExecuteCall`), so the helper stores a pending `FrameResult`. `advanceCall` and the outer `Advance` consume and clear it; `ApplyOutcome` leaves it for the next `Advance`. This preserves exactly-once completion without a second pipeline path.

**Alternative rejected:** applying `exit`/`end` by throwing or by setting interpreter state directly — the frame tree would remain mid-advance, and the call-stack ordering that currently guarantees exactly-once export would be broken.

### D5 — `end` is a distinct `FrameResult`; `exit` is an ordinary completion

```cpp
struct Ended { StepDataValue value_; };
using FrameResult = std::variant<Suspended, Completed, Ended, Failed>;
```

- A sequence that applies `end` returns `Ended` with the step's post-pipeline value.
- `advanceContainer` propagates `Ended` immediately, before applying the enclosing `do`/`fork` step's `output.as` or `export.as`.
- `ForkFrame` propagates `Ended` from a branch and abandons the remaining branches.
- `terminalize` treats `Ended` like `Completed`: document `output.as` still shapes the final value, then the run succeeds.

`exit` marks the sequence completed with `Completed{data_}`; its parent container applies its own pipeline and directive normally. This is the difference between "leave this scope" and "stop the workflow".

```
do (parent step)                         root
  +- nested sequence                     |
       step X: then: end  --> Ended(V) --+--> Ended(V) --> terminalize
       (X's output.as/export ran)            (parent do's output.as/export skipped)
```

### D6 — The synchronous guard lives per sequence and resets per `Advance`

`SequenceFrame` keeps a transition counter, reset at the top of every `Advance` and incremented once per loop iteration. Exceeding a generous implementation-defined constant fails the current step with `INTERNAL` and a message naming the step.

Why per sequence rather than one counter per `drive()`:

- a `then` jump cannot cross scopes, so the loop is always visible to the sequence that owns it;
- a wide fork of synchronous branches is not a loop and must not trip the guard (each branch is its own sequence);
- a loop containing a `call` suspends, so the next `Advance` resets the counter — asynchronous loops remain legal and are not bounded by this rule.

A per-sequence counter does not protect a parent loop that re-enters child sequences each iteration only if the parent's counter is the one that grows — and it is, because the parent's `Advance` loop is what iterates. Children reset independently, which is exactly what avoids false positives.

**Alternatives rejected:** a global per-drive counter conflates wide forks with loops; validation-time cycle detection is unsound because synchronous calls may legitimately form a loop and still yield.

### D7 — Validation mirrors the runtime scope tree

`ValidateDocument` becomes scope-aware. Each `Tasks` collection builds a name set once (rejecting duplicates) and passes the corresponding span to each task for `then` resolution. Recursion:

```
document.do                     scope A
  do task -> its body           scope B
  fork task -> branch i         scope Ci = { branch task i } only
                 branch do      scope Di = that do body
```

Each fork branch is validated as a one-task scope, matching `ForkFrame`'s single-task `SequenceFrame`, so a branch task cannot target a sibling branch. That one-task scope has a practical consequence: a branch that is directly a `switch` has no sibling work to select. Its case directives can only continue, exit, end, or target itself; `continue` and `exit` both merely complete the branch, so the only materially useful direct-branch directive is `end`. A branch that needs to select among alternative steps declares a `do`, whose body is the real multi-task scope; this is the idiomatic shape and the one the branch tests exercise.

The existing checks remain: call target/key set/requirement kind, typed-slot syntax and wrapping, empty competing fork, export-inside-branch. New checks:

- every switch case `when` compiles as a `kSwitchWhen` typed slot;
- every switch case has a non-empty `then`;
- at most one case omits `when`;
- every `then` resolves through `ResolveFlowDirective` in its own scope;
- `set` data compiles through the literal-capable `Validate` (partial wrappers rejected);
- no duplicate task names in a scope.

Expression syntax is checked before shape checks, matching the call argument path, so a malformed program is never masked by a structural error in the same node.

### D8 — `set` evaluation reuses the literal-capable evaluator

`EvaluateSet` mirrors `transformInput`'s subject handling:

- object/scalar → `openworkflow::Value`;
- if the input is JSON, use it as the subject;
- if the input is opaque and the value contains no expressions, use JSON `null` as the placeholder subject (literal-only sets need no subject);
- if the input is opaque and the value contains an expression, fail with the existing "the data subject is not JSON" error;
- evaluate via `ExpressionEngine::Evaluate` with `$context`, then `StepDataValue::FromValue`.

This keeps one expression rule: wrapped strings evaluate, bare strings are literal, partial wrappers are validation errors. A set result is always JSON by construction.

### D9 — `switch.when` is a new typed slot

`TypedSlot::kSwitchWhen` joins the enum, binds only `$context` (`typedSlotVariables` returns the same set as `kIf`), and is validated with `ValidateTypedSlot`. Runtime evaluation uses `EvaluateTypedSlot` and requires a boolean, with the error naming both the switch task and the case.

## Risks / Trade-offs

- **[Documents that previously ignored `then` now execute it, and unresolved targets now fail validation]** → Intended behavior change; the proposal's migration notes call it out. Existing documents without `then` are unaffected.
- **[Duplicate-name rejection can invalidate previously accepted documents]** → Those documents had ambiguous jump targets; rejecting them early is preferable to silently choosing one. The check is scoped, so unrelated sequences may reuse names.
- **[`end` skips enclosing `output.as`/`export.as`]** → The literal reading of "conclude the workflow"; documented and tested. If authors need enclosing shaping, they should use `exit` and an explicit final task instead.
- **[A synchronous loop with no child still consumes CPU until the guard trips]** → The budget makes the failure bounded and observable; choosing a generous constant limits false positives.
- **[Asynchronous loops remain unbounded in total work]** → Deliberate: they yield to `io_uring`, so they do not starve the node. A run-wide cap belongs with cancellation/timeouts, not here.
- **[Strict boolean `when` diverges from jq truthiness]** → Consistent with `if`; loose mode is the eventual place for a relaxed rule.
- **[A scalar `set` with a YAML number becomes a string]** → Parser behavior predates this change; map form preserves scalar kinds. Fixing it would be an SDK change and is deferred.
- **[Validation and runtime scope drift]** → Mitigated by one shared `ResolveFlowDirective` and by modeling fork branches as one-task scopes in both layers; covered by cross-scope validation tests.

## Migration Plan

No wire, config, proto, or storage migration. Implement in dependency order, keeping the suite green:

1. `expression.{hh,cc}`: `TypedSlot::kSwitchWhen`, name, variable mapping.
2. `frame.{hh,cc}`: `FlowDirective`, `Ended`, directive resolution/applying helper, per-sequence guard, `Ended` propagation in sequences and forks.
3. `interpreter.{hh,cc}`: `EvaluateSet`, `SelectSwitchDirective`, directive/end terminalization, scope-aware validation, duplicate-name and switch/set/`then` checks.
4. `frame_test.cc` / `interpreter_test.cc` fakes: new `FrameContext` methods.
5. Tests: unit coverage per requirement, CTK-shaped e2e workflows (`set`, `flow`, `switch`), and the guard self-loop.
6. `docs/testing-openworkflow.md`: runnable examples for set/switch/then.

Rollback is reverting the change; no external state or artifact format is involved.

## Open Questions

- The exact synchronous-transition constant and the wording of the guard failure are implementation details, not spec-level decisions.
- Whether a future change adds a global run-wide step cap or a wall-clock timeout for asynchronous loops is deferred.
