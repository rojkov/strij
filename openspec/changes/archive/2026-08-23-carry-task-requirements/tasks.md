## 1. Protos

- [x] 1.1 Add `ResourceRequirements requirements = 5` to `Task` in `api/core/task/task.proto` with a comment stating it is the authoritative requirements source (resolved at the gateway)
- [x] 1.2 Remove `default_resources` from `HandlerCapability` and `HandlerCapacity` in `api/core/node/capabilities.proto`; update the `HandlerCapacity`/`function_sourced` comments to say requirements are resolved at the gateway and carried on the `Task`

## 2. Gateway

- [x] 2.1 In `GatewayHttpHandler::HandleMessage`, set the resolver output on the task (`*task.mutable_requirements() = ...`) right after parameters are populated, before `Choose()`; pass the same carried field to `TaskOffer` and `ExactStateTracker::RecordSubmission`
- [x] 2.2 Update/add gateway tests: resource headers land in `Task.requirements`, no-resource requests carry empty requirements, frame round-trips with requirements intact

## 3. Nodeagent

- [x] 3.1 Delete `resolveRequirements()` from `NodeagentTlvHandler` (.hh/.cc); call `admission_->Admit(task.type(), task.requirements())`
- [x] 3.2 Remove default-resources copying from `capabilities.cc` / capabilities building; adjust `ParseConfig` overrides in echo and piped-executable factories to return concurrency-only `HandlerCapacity`
- [x] 3.3 Update nodeagent tests: admission reserves exactly the carried requirements; absent-field task admits without pool reservation but respects concurrency limit; pool-exhaustion rejection still fires based on carried value

## 4. Config, docs, examples

- [x] 4.1 Drop `capacity.default_resources` blocks from `config/examples/nodeagent.yaml` and `config/examples/kubernetes-configmap.yaml`
- [x] 4.2 Update `docs/config.md` handler-capacity section (concurrency only; note resources are declared per-request via `x-strij-resources-*` headers)
- [x] 4.3 Update config-loader tests that reference `default_resources` (`test/core/config/config_loader_test.cc`) to concurrency-only capacity expectations

## 5. Capability advertisement tests

- [x] 5.1 Update `nodeagent_capabilities_test.cc`: `HandlerCapacityCarriesDefaultResources` becomes a concurrency-only assertion; advertisement contains no `default_resources`

## 6. Roadmap & validation

- [x] 6.1 Mark the ROADMAP.md open question about `default_resources` × repo-resolved requirements as resolved (pointer to this change)
- [x] 6.2 Run `make build && make test`; run an end-to-end smoke: gateway + one nodeagent, submit task with `x-strij-resources-cpu` header exceeding pool total, observe `kTaskRejected` reason referencing the pool
