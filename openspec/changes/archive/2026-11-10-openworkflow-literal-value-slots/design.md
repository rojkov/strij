# Design

## Context

See `proposal.md` — Why.

The interpreter's expression layer has two evaluation modes today, but they are welded to the wrong axis:

- `ValidateTypedSlot` / `EvaluateTypedSlot` handle eight `TypedSlot` values. Every string in the tree must be a whole `${ }` program; `$input`/`$output` are bound by an enum branch for `kTaskOutput`/`kTaskExport`.
- `Validate` / `Evaluate` handle the literal-capable slots (`with`, `set` data, non-string `for.in`, `emit.data`, `until` filters). A `${ }`-wrapped string runs, a bare string stays literal, a partial wrapper is an error.

The spec already contains the rule this change needs: *"In strict mode, only `${ }`-wrapped strings are expressions"* says every other string is literal text, uniformly, at every position. The typed-slot requirement then carves out nine declaration sites that instead reject bare strings. The tension is resolved by narrowing the carve-out to the slots that must produce a single typed result.

Archived design `2026-10-21-openworkflow-task-data-shaping` (D3) chose the uniform rule to stop `if: .enabled` from silently meaning "always true". That rationale does not extend to slots whose purpose is to *build a value*; the value-construction slots need literal constants to be useful at all.

Constraints: one event-loop thread, validation is a pre-execution pass with no I/O, and the interpreter must report exactly one final result. The expression engine's recognition helpers (`IsExpression`, `HasPartialWrapper`, `ExpressionProgram`) and the jq-backed evaluator are unchanged.

## Goals / Non-Goals

**Goals:**

- One rule per slot family: expression-only slots reject literal text; value-construction slots accept literal text and expression leaves.
- A single validate/evaluate path parameterized by a slot descriptor, replacing the two near-duplicate code paths.
- Identical subject semantics for `input.from`, `output.as`, and `export.as`: a JSON subject is required only when the slot contains an expression.

**Non-Goals:**

- `evaluate.mode: loose`, or any per-slot opt-in flag.
- A literal escape syntax for `${` inside literal text.
- Changing `output.as`/`export.as` result typing, `$secrets`/`$task`/`$workflow`/`$runtime` bindings, or any wire/protobuf shape.
- Changing the parser: it already preserves the string-or-object form of these fields.

## Decisions

### D1 — Slot taxonomy follows the value, not the shape

Expression-only slots are the ones whose result must be a single typed value: `if`, `switch.when`, `for.while`, and the string form of `for.in`. Value-construction slots build a value tree: `input.from`, `output.as`, `export.as`, document `input.from`, document `output.as`.

| Slot | Literals | Variables |
|---|---|---|
| `if`, `for.while` | forbidden | `$context` (+ loop locals) |
| `switch.when` | forbidden | `$context` |
| `for.in` (string form) | forbidden | `$context` (+ enclosing loop locals) |
| `for.in` (non-string form) | allowed | same |
| `input.from` (task, document) | allowed | `$context` (+ loop locals) |
| `output.as` (task, `for` body, document) | allowed | `$context`, `$input`, `$output` |
| `export.as` (task, `listen.foreach`) | allowed | `$context`, `$input`, `$output` |

**Alternatives considered.** Per-form split (root string expression-only, object/array members literal-capable) would keep the `input.from: .payload` footgun loud, but it invents a second rule inside one slot, still forbids a literal root body, and diverges from `with`/`set` whose bare strings are literal. The settled choice is uniform literal-capable: "strict" governs *recognition* of expressions, not *permission* to use text. A bare `.payload` is now a literal body, which surfaces downstream as a malformed or empty body rather than at validation.

### D2 — Replace `TypedSlot` with an `ExpressionSlot` descriptor

After D1 the remaining `TypedSlot` members (`kIf`, `kSwitchWhen`, `kForIn`, `kForWhile`) are behaviorally identical: all bind `$context` plus locals and forbid literals. The enum would only supply a display string, so it is replaced by data:

```cpp
enum class SlotVariables : std::uint8_t { kContext, kContextInputOutput };

struct ExpressionSlot {
  std::string_view name_;   // display name used in errors
  SlotVariables variables_;
  bool literals_;
};

inline constexpr ExpressionSlot kIf{"if", SlotVariables::kContext, false};
inline constexpr ExpressionSlot kInputFrom{"input.from", SlotVariables::kContext, true};
inline constexpr ExpressionSlot kTaskOutput{"output.as", SlotVariables::kContextInputOutput, true};
// ...
```

`Validate(value, slot, arguments)` and `Evaluate(value, slot, subject, arguments)` become the only two entry points. `TypedSlotName`, `typedSlotReservedNames`, `collectTypedPrograms`, and the `$input`/`$output` enum branch in `EvaluateTypedSlot` all disappear.

```
interpreter            expression engine                 evaluator (jq)
    |                        |                                |
    | Validate(value, slot)  |                                |
    |----------------------->|  collect programs              |
    |                        |  partial wrapper -> error      |
    |                        |  bare string + !slot.literals_ |
    |                        |      -> error                  |
    |                        |------------------------------->| compile
    |    ok / MALFORMED      |<-------------------------------|
    |<-----------------------|                                |
    |                        |                                |
    | Evaluate(value, slot,  |                                |
    |          subject,args) |                                |
    |----------------------->|  bind slot.variables_ + locals |
    |                        |  literal leaves pass through   |
    |                        |------------------------------->| run
    |   Value (JSON-faithful)|<-------------------------------|
    |<-----------------------|                                |
```

**Alternatives considered.** Renaming the enum in place (`ExpressionOnlySlot`) keeps two code paths and forces "which variables" to stay an enum axis, which is exactly what blocks widening to `output.as`/`export.as`. Removing the enum outright (`std::string_view slot_name` at call sites) loses the variable/policy data and pushes it back into caller-side branches.

### D3 — Results stay JSON-faithful

A value-construction slot always produces a JSON value: literals, expression results, and mixed trees go through `ValueToJv(...).Dump()`. A slot whose whole value is the literal string `hello` therefore yields the body `"hello"`, quotes included — the same as `${ "hello" }` today and the same as `output.as`.

**Alternatives considered.** Treating a root literal string as opaque body bytes would be friendlier for `/bin/cat`-style children, but it splits `input.from` from `output.as`, reintroduces the two-representation drift that `2026-10-21-openworkflow-task-data-shaping` D2 removed, and makes `$input` ambiguous. The quoting is documented instead.

### D4 — One subject rule for all value-construction slots

`transformInput` already evaluates a literal-only slot without a subject and fails only when an expression needs one. `transformOutput` and `ExportTaskResult` currently fail first, unconditionally:

```cpp
if (!input.IsJson())      return "the task input is not JSON";
if (!raw_output.IsJson()) return "the output subject is not JSON";
```

Both adopt the `input.from` rule: the JSON precondition applies only when the slot tree contains at least one expression. Detection reuses `CollectExpressions`; a partial wrapper is already rejected during validation with the slot-named error, so the runtime scan only decides whether a subject is required. `$input`/`$output` are supplied to the evaluator as before and are only referenced by expressions, so a literal-only slot never observes an unbound variable.

**Alternatives considered.** Keep the unconditional precondition: minimal spec churn, but then `input.from: { a: 1 }` works over opaque input while `output.as: { a: 1 }` fails over opaque output — an inconsistency with no user-facing justification.

### D5 — Error messages keep the slot name

The literal-capable path's partial-wrapper error currently omits the slot (`"the ${ } wrapper must span the whole value, but ..."`), while the typed path names it. With one path, the validation-time message formats `slot.name_`, so `input.from` vs `output.as` vs `document output.as` remain distinguishable in a report; the slot-agnostic `CollectExpressions` scan used by the runtime subject checks keeps the generic message, which validation has already superseded.

## Risks / Trade-offs

- **A bare `.payload` in a value-construction slot is now a literal body.** Accepted by decision. It surfaces as a malformed body at execution rather than a validation error; the doc note on JSON-faithful literal strings covers the visible symptom.
- **Literal text containing `${` is still rejected** (partial wrapper), so a shell script body cannot be written as a bare literal block. → Unchanged from today; the workaround is a whole-value expression (`'${ "echo ${HOME}" }'`). No new escape syntax is introduced.
- **`output.as`/`export.as` literal-only over opaque values now succeed where they used to fail.** → This is the intended expansion; the spec scenario pins it, and the expression-bearing case keeps the old failure.
- **Vocabulary drift**: specs and comments say "expression-typed" in places. → The delta rewrites the requirement as "expression-only"; code comments use the same term.

## Migration Plan

No data or wire migration. Documents that executed before are unaffected because every string in those slots was already a whole-value expression; only previously-rejected documents become executable. Deploy is a normal nodeagent rollout; rolling back restores the malformed-document error for bare value-slot strings.

## Open Questions

None.
