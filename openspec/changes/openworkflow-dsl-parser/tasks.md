# Tasks

## 1. Package scaffolding

- [ ] 1.1 Create `src/openworkflow/BUILD.bazel` with the model library target (e.g. `openworkflow_model_lib`) using `strij_cc_library`, depending on nothing but std, and verify `bazel build //src/openworkflow:openworkflow_model_lib` succeeds with an empty/placeholder header
- [ ] 1.2 Create `src/openworkflow/parser/BUILD.bazel` with the parser library target (e.g. `openworkflow_parser_lib`) via `strij_cc_library` depending on `@yaml_cpp` and `:openworkflow_model_lib`, and verify the build target compiles
- [ ] 1.3 Create `test/openworkflow/BUILD.bazel` + `test/openworkflow/parser/BUILD.bazel` test targets via `strij_cc_test` (adding `@googletest//:gtest_main` explicitly) and verify `bazel test //test/...` discovers them

## 2. Model DTOs

- [ ] 2.1 Implement `value.hh` (`Value` variant: null/bool/int64/double/string/array/object, recursive) with no yaml includes and verify it compiles standalone and round-trips its alternatives in a unit test
- [ ] 2.2 Implement `types.hh` (Duration/DurationRef, Input, Output, Export, Schema, ExternalResource, Endpoint, Error/ErrorFilter, Timeout, RetryPolicy/Limit/Backoff/Jitter, Catalog, Authentication + OAuth2/OAuth2Token) and verify it builds as part of the model lib
- [ ] 2.3 Implement `events.hh` (EventProperties, EventConsumptionStrategy, EventFilter, Correlation, SubscriptionIterator) and verify it builds
- [ ] 2.4 Implement `document.hh` (Document, DocumentInfo, Schedule, Evaluate) and verify it builds
- [ ] 2.5 Implement `task.hh` (Task, TaskBody, NamedTask, the 12 task bodies, ForSpec, NamedCase, Catch, Run processes, ContainerLifetime) and verify it builds
- [ ] 2.6 Implement `use.hh` (Use, Extension) and verify it builds

## 3. Parser glue

- [ ] 3.1 Implement `parser/errors.hh` (`ParseError : YAML::Exception` with `position` carry-over) and verify a unit test constructs and formulates it
- [ ] 3.2 Implement `parser/one_of.hh` (`KeySet` + `SelectOne`) and verify it throws positioned `ParseError` on zero or >1 present keys in a unit test
- [ ] 3.3 Implement `parser/decode.hh/.cc` for the leaf/shared shapes (`Value`, `DurationSpec`, `DocumentInfo`, `Schedule`, `Evaluate`, Authentication/OAuth2, retry types, endpoint/resource/schema) and verify decoding parses representative YAML in unit tests
- [ ] 3.4 Implement `parser/decode.hh/.cc` for `Tasks`/`NamedTask` (both sequence-of-single-key-maps and map forms, order-preserving) and verify both forms parse in order-preserving unit tests
- [ ] 3.5 Implement `parser/decode.hh/.cc` for the `Task` union via `SelectOne` (12 kinds) and all task bodies, and verify each kind parses with its common properties
- [ ] 3.6 Implement `parser/decode.hh/.cc` for `Document` (top-level required + optional sections) and `Use` components, verifying multi-`Use`-category YAML parses
- [ ] 3.7 Implement `parser/parse.hh/.cc` (`Parse(text) -> std::expected<Document, ParseError>`, single catch converting `ParseError` then `YAML::Exception` with `mark.pos`) and verify the entry point is exception-free from the caller's perspective
- [ ] 3.8 Confirm strict unknown-key rejection is enforced by every typed `convert` specialization and verify a typo key produces a positioned error naming the key

## 4. Behavior tests

- [ ] 4.1 Golden tests: add the DSL reference's own example documents (document/do/for/fork/try/switch/set/wait/listen/emit/raise/run/http-call/oauth2/use-extensions examples) as test fixtures and verify `Parse` succeeds on each
- [ ] 4.2 Error-contract tests: YAML syntax errors, missing required keys, and structural violations all yield `expected`-error with `position` inside the input and a non-empty message naming the construct, with no exception escaping `Parse`
- [ ] 4.3 One-of tests: task bodies, `run` process kinds, auth schemes, schema source, consumption strategy, backoff branches — each with a valid case and a zero/multi-key failure case (spec scenarios "Multiple task kinds", "Multiple authentication schemes")
- [ ] 4.4 Raw-preservation tests: bare-vs-`${}`-wrapped expressions, string-vs-object durations, and unresolved `timeout`/`error`/`retry` string references (spec scenarios "Bare and wrapped expressions", "Duration accepts both forms", "Reference fields stay unresolved")
- [ ] 4.5 Opaque-fidelity tests: `Value` slots preserve bool/int/double/string scalars and nesting (spec scenario "Mixed scalar kinds round-trip")
- [ ] 4.6 Deferred-semantics tests: `return: banana`, `read` without `on`, dangling `then` targets, and nonexistent `use.*` references all parse successfully (spec requirement "Semantic rules are deferred")
- [ ] 4.7 Unknown-key tests: each typed construct (document, task, component, sub-object) rejects unknown keys and `metadata`/opaque slots accept arbitrary content (spec scenario "Unknown key on a typed construct fails")

## 5. Final verification

- [ ] 5.1 Run `make build` and `make test` and confirm the OpenWorkflow SDK targets compile and all `test/openworkflow/...` tests pass
- [ ] 5.2 Run `make check` (include purity + namespace doctrine) and confirm `src/openworkflow` headers pass include purity (no yaml/absl/protobuf in model headers) and the `strij::openworkflow` namespace maps to the `src/openworkflow` directory