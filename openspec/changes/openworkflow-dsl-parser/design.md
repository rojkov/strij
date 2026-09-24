# Design

## Context

See proposal.md — Why. The change introduces a standalone Open Workflow DSL
parser SDK under `src/openworkflow/`. Two constraints shape everything:

- The SDK is built to be **externalized later** (own `openworkflow` namespace,
  own library, dependency of strij) — so it must not lean on strij internals.
  Its only dependency is `yaml-cpp` plus std.
- The SDK is **structure-only by contract** (see spec
  `openworkflow-dsl-parser`): one-of discriminators, required fields, ordered
  collections, and faithful opaque slots. Enum validation, cross-field rules,
  reference resolution, and expression interpretation belong to a future
  interpreter.

The existing `expression-evaluation` change supplies the interpreter half
(jq evaluator); this change supplies the parse half.

## Goals / Non-Goals

**Goals:**

- YAML-free DTO model in `src/openworkflow/` mirroring the DSL reference, with
  every public header free of yaml-cpp types.
- Single entry point `Parse(text) -> std::expected<Document, ParseError>`
  that never throws.
- Decode spine built from `YAML::convert<T>` specializations, so parses are
  terse, recursive, and idiomatic to yaml-cpp.
- Position-carrying errors (`byte offset` into the input text) with
  human-readable messages.

**Non-Goals:**

- Matching the DSL reference's *inner* shapes for retry backoff branches
  (constant/exponential/linear) — the reference defines them only as `object`;
  payloads are kept opaque until a concrete schema appears.
- Multi-error reporting; one error per failed parse.
- Any semantic/cross-field validation.
- Any strij-internal reuse (no abseil, protobuf, strij headers).

## Decisions

### D1 — yaml-cpp is the front-end only; the model never sees yaml

Text enters through `YAML::Load` (occurring behind the one catch/convert in
`Parse`), all decoding happens on `YAML::Node`, and the model stores opaque
content in a self-defined `Value` type — never `YAML::Node`.

This keeps `value.hh`/`document.hh`/`task.hh`/etc. pure `std::variant`/
`std::optional`/`std::vector` types. Alternatives considered: exposing
`YAML::Node` in opaque slots (cheap but couples every future consumer,
including the jq-interpreter, to yaml-cpp); a proto model (fights the DSL's
expression-any nature and the SDK's externalization goal).

### D2 — exceptions inside, `std::expected` at the door

`YAML::convert<T>::decode` returns `bool`, and `false` collapses to yaml-cpp's
generic "bad conversion" — the message is lost and the blame is wrong for
one-of failures (they are *set*-level or *missing-key* errors, not
single-node errors). So the internal spine **throws** a
`ParseError : YAML::Exception` carrying a yaml-cpp `Mark` and a formatted
message. `Parse()` catches `ParseError` (our structural errors) then
`YAML::Exception` (yaml syntax + scalar mismatches) and converts to
`std::expected<Document, ParseError>`. Exceptions never cross the public API.

Alternatives considered: an SDK-owned `Decoder<T>` template returning
`std::expected` throughout (exception-free everywhere, but roughly twice the
code and lost yaml-cpp scalar-mismatch integration). Since the public contract
is value-based either way, and neither error path allows recovery or
aggregation, the shorter throw-based spine wins.

### D3 — spine throws, leaves probe

`TryConvert<>(T&)` (bool-returning) is used where decode is *inquiry*, not
*commitment*: `Value`'s scalar-kind discrimination (bool → int64 → double →
string) and any lenient re-checking. Everything structural uses `as<T>()` or
`convert<T>::decode`, which throws. The two never mix.

### D4 — `SelectOne` helper for every exactly-one-of

A single template `SelectOne(node, KeySet, what)` checks present top-level
keys (`IsDefined()`) and throws a positioned `ParseError` on zero or >1. It
backs all five discriminated unions: the task body (12 kinds), `run` process
(4 kinds), auth scheme (5), schema source (2), consumption strategy (3), and
backoff branch (3). The apparent variation between the task kinds (e.g. `call`
scatters `with` as a sibling; `run` nests under `run:`) is absorbed by giving
every branch decoder the **whole task node**, not just the discriminator
subtree.

### D5 — collections are ordered `std::vector<NamedTask>`

`do`-like collections are typed `map[string, task]` in the reference but
written as sequences of single-key maps; declaration order is meaningful for
sequential execution. `DecodeTasks` accepts both the sequence-of-single-key-maps
form and a genuine map form, yielding `std::vector<NamedTask>`. `use.functions`
alone stays a `std::map<string, Task>`.

### D6 — string-or-object unions preserved as `std::variant`, never resolved

Reference fields (`timeout`, error refs, retry refs, authentication refs) and
expression-or-data fields (durations, `set.data`, `input.from`, `output.as`)
decode to `std::variant<std::string, X>` / `std::variant<std::string, Value>` —
the parser never looks up `use.*`, never classifies a string as an expression,
never converts a duration. Interpretation is the interpreter's job.

### D7 — strict unknown-key rejection

Typed constructs reject keys the DSL does not define (positioned error naming
the key). Optional preset: none — the SDK's *only* sanctioned lenience is
`metadata` and the opaque `Value` slots, which accept anything by design. This
is the structural layer's main typo-catching surface (`dok:` vs `do:`). The
collapsed `EventProperties` (no `additional_` bag) means CloudEvents extension
attributes are treated as unknown keys and rejected.

### D8 — enum-valued fields are `std::string`

`grant`, `return`, `pull_policy`, `language`, `mode`, `cleanup`, `extend`, and
friends decode to strings; allowed-value sets are enforced (when at all) by the
interpreter. Chosen over `enum class` because values are author-facing literal
strings and the SDK is structure-only.

### D9 — every DTO member carries a trailing underscore

All members use `name_` (including `do_`, `if_`, `in_`, `return_`, `export_`
which dodge C++ keywords) — matching the repo-wide convention.

### D10 — backoff branches keep opaque payloads

`Backoff` enforces the constant/exponential/linear one-of structurally, but each
branch stores its subtree as a `Value` because the reference enumerates no inner
keys (grep-verified; only `exponential: {}` appears). When a concrete schema
arrives, the branch payloads can type up without touching the one-of machinery.

## Component Layout

```
src/openworkflow/
├── value.hh                 Value (JSON-faithful variant)
├── types.hh                 Duration/Spec, Input, Output, Export, Schema,
│                            ExternalResource, Endpoint, Error/ErrorFilter,
│                            Timeout, RetryPolicy, Catalog, Authentication(+OAuth2)
├── events.hh                EventProperties, EventConsumptionStrategy,
│                            EventFilter, Correlation, SubscriptionIterator
├── document.hh              Document, DocumentInfo, Schedule, Evaluate
├── task.hh                  Task, TaskBody, NamedTask, the 12 bodies, Catch, Run processes
├── use.hh                   Use, Extension
└── parser/
    ├── errors.hh            ParseError
    ├── one_of.hh            SelectOne + KeySet
    ├── decode.hh/.cc        YAML::convert<…> specializations (+ select one_of)
    ├── parse.hh/.cc         Parse(text) → expected<Document, ParseError>
    └── BUILD.bazel          owd_parser_lib (openworkflow_model_lib + parser glue)
```

Header-dependency rule: **model headers never include `parser/` or yaml-cpp**;
only `parser/decode.cc` and friends include yaml. Replacing the front-end later
means rewriting only `parser/`.

## Parse flow

```
 Parse(text : string_view)
   │
   ▼
 try { YAML::Load(string{text}) }        ← yaml syntax errors → catch → ParseError{mark.pos, what()}
   │
   ▼
   .as<Document>()                        ← recursively converts:
   │
   │    convert<Document>::decode            attach subnodes
   │      ├─ convert<DocumentInfo>::decode   required fields → throw ParseError on missing
   │      ├─ convert<Tasks>::decode          `[ {name: task} ]` XOR `{name: task}` → vector<NamedTask>
   │      └─ convert<Task>::decode
   │           └─ SelectOne(task, kTypes, "task type")     ← 0 or >1 → throw ParseError{mark.pos, msg}
   │                └─ convert<Run>/…::decode(task)        ← branch reads whole task node
   │                     └─ convert<Value>::decode          ← TryConvert bool→int64→double→string (no throw)
   │
   ● all decode failures throw ParseError : YAML::Exception
   ▼
 catch (ParseError&)            → Unexpected(ParseError{pos, msg})
 catch (YAML::Exception&)       → Unexpected(ParseError{pos, what()})      // scalar mismatches etc.
   ▼
 expected<Document, ParseError>
```

## Risks / Trade-offs

- **[Exceptions across an internal library boundary]** → The public API is
  exception-free by contract; exceptions are confined to `parser/`. If the SDK
  is externalized, `Parse()`'s expected-envelope survives unchanged.
- **[Coupling of the decode layer to yaml-cpp]** → Isolated in `parser/`;
  the model is yaml-free by D1, so a front-end swap only rewrites `parser/`.
  The `ParseError : YAML::Exception` base also stays internal.
- **[Reference drift (DSL 1.0.3) and the undocumented backoff internals]** →
  Golden tests are derived from the reference's own example corpus, so drift
  and regressions surface in tests; opaque `Value` payloads (D10) shield the
  model from guessing.
- **[Strict unknown-key rejection may veto valid extensions]** → Bound to the
  spec's closure about task types and `metadata`-only free-form; easy to loosen
  to warn-and-skip later if a sanctioned extension point appears.
- **[First-error only]** → Matches the spec; aggregate diagnostics would
  require a different (walker-based) decode spine and is consciously excluded.

## Migration Plan

New code and new Bazel packages only; no existing `src/` targets change.
Externalization (namespace `openworkflow`, own repo/dependency) is a
mechanical move gated behind future integration with the workflow task
handler — no coexistence requirement is designed in now beyond keeping the
model yaml-free.

## Open Questions

- Whether a lenient "warn-only" unknown-key mode is ever wanted by consumers;
  deferrable — it would be an additive option after v1, not a change to the
  default behavior this design specifies.
- Whether to pin an exact OWD reference/SDK version string in `document.dsl`;
  currently preserved verbatim (spec: expressions preserved raw) and left to
  the interpreter to interpret.