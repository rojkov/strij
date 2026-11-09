# Proposal

## Why

The value-construction slots (`input.from`, `output.as`, `export.as`, and the document forms) currently refuse literal text at every depth: every string in the tree must be a whole `${ }` expression. That makes their object form unusable for real payloads — a task cannot send a constant `content-type`, a fixed `mode`, or inline source code without writing `'${ "…" }'` for each one. The rule was designed to keep a bare `if: .enabled` from silently meaning "always true"; applying it to slots whose job is to *build a value* exported a predicate guard to places it does not belong, and left the documented `pyfunction` example in `docs/testing-openworkflow.md` unexecutable.

## What Changes

- **BREAKING**: the value-construction slots become literal-capable value trees. A `${ }`-wrapped string is evaluated; a bare string is literal text; a partially wrapped string remains a malformed document. Affected slots: a task's `input.from`, `output.as`, `export.as`, the document's `input.from`, and the document's `output.as`.
- The expression-only rule narrows to the slots that must produce a single typed value: a task's `if`, a switch case's `when`, a `for` task's `while`, and the string form of a `for` task's `in`.
- A literal-only `output.as` or `export.as` no longer requires a JSON subject. Expressions over an opaque value still fail with a subject error, matching the rule `input.from` already follows.
- Evaluation stays JSON-faithful: a root literal string body is a JSON string (quoted), consistent with `output.as` and with expression results.
- Internally, `TypedSlot` is replaced by an `ExpressionSlot` descriptor (display name, bound variables, literal policy) with a single validate/evaluate path. This is structural support for the widened slots, not a behavior change.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `openworkflow-interpreter`: the "expression-typed slots accept only whole-value wrapped expressions" requirement is rewritten to cover only the scalar/predicate slots, the literal-capable rule is extended to the value-construction slots, and the opaque-subject requirement for `output.as`/`export.as` becomes conditional on the slot containing an expression.

## Impact

- `src/nodeagent/extensions/task_handlers/openworkflow/expression.{hh,cc}`: `TypedSlot`, `ValidateTypedSlot`, `EvaluateTypedSlot`, and `TypedSlotName` are replaced by slot descriptors and one literal-policy-aware validate/evaluate path; `$input`/`$output` binding becomes slot data instead of an enum branch.
- `src/nodeagent/extensions/task_handlers/openworkflow/interpreter.cc`: seven validation and six evaluation call sites move to the new API; `transformOutput` and `ExportTaskResult` adopt the conditional subject rule already used by `transformInput`.
- Tests under `test/nodeagent/extensions/task_handlers/openworkflow/` (`expression_test`, `interpreter_test`, and any document-level slot tests) are updated for the inverted scenarios and the new API.
- `docs/testing-openworkflow.md`: the `openworkflow-pyfunction` example (committed as "add wrong example") becomes valid as written; no doc edit is required, and it serves as the acceptance case.
- No wire-protocol, protobuf, scheduler, admission, or `pyfunction` contract changes. The `openworkflow-dsl-parser` behavior is unchanged: it already preserves the string-or-object form of these fields.
