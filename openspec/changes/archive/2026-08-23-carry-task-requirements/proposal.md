## Why

Task hardware requirements are currently resolved twice from two different sources that never reconcile: the gateway derives them from task parameters (used for routing and shadow accounting, then discarded), while the nodeagent independently falls back to operator-declared `default_resources` for admission. The values can disagree — a client declaring `cpu=4` gets routed to a node with 4 free cores but admitted against a cpu=1 default, silently overcommitting; a client declaring nothing gets routed onto any node yet rejected by node-side defaults. Carrying the resolved requirements on the `Task` makes the wire value the single source of truth for routing, gateway shadow accounting, and node admission — and gives future fire-and-forget scheduler extensions (`Schedule()`) and Sparrow-style probes an authoritative requirements field to send without any node-side knowledge of task semantics.

## What Changes

- **BREAKING**: `strij.task.Task` gains a `ResourceRequirements requirements = 5` field; the gateway sets it after resolution and every consumer reads requirements exclusively from the task message.
- **BREAKING**: `default_resources` is removed from `HandlerCapability` and `HandlerCapacity` protobufs, from `NodeAgentConfig` handling, from example configs, and from docs. Node-side requirements fallback ceases to exist. No migration path needed (no production clusters).
- The nodeagent deletes its `resolveRequirements()` path; admission admits against `task.requirements()`. A task submitted without the field (foreign/direct TLV submitter) is treated as empty requirements — unconstrained by pools, per-type concurrency limits still apply (identical to how an undeclared type behaves today).
- The gateway resolves once, before scheduling (resolve-before-route): resolution is parameter-driven only, so nothing cluster-dependent has to be known pre-route. Ordering is compatible with replacing `Choose()` by a scheduler-owned `Schedule()` later.
- Gateway `ExactStateTracker` records the same requirements object that travels on the wire; gateway shadow accounting stops drifting from node truth.
- `RequirementsResolver` remains a gateway-side extension seam (v1: `ParamsOnlyRequirementsResolver`); the function plane will plug in there. Nodes hold no requirements-resolution responsibility beyond trusting the wire value.
- Pools, reservations, per-type concurrency, and all node-side capacity enforcement stay unchanged — this removes the *defaults* mechanism, not node authority over capacity. Admission still caps carried requirements against advertised pool totals.
- Closes the ROADMAP open question "how `default_resources` interacts with repo-resolved requirements when both exist" (answer: defaults are gone; the resolver is the sole authority).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `task-protocol`: `Task` message schema gains the `requirements` field (`ResourceRequirements`, field 5) carrying resolved hardware requirements.
- `gateway-task-bridge`: `GatewayHttpHandler` SHALL resolve requirements and set them on the `Task` before scheduling and submission; the sent frame carries them.
- `node-advertisement`: `HandlerCapability` drops `default_resources` (fields: `task_type`, `concurrency`, `function_sourced` only).
- `nodeagent-config`: `HandlerCapacity` shrinks to concurrency only; config loading no longer accepts or propagates `default_resources`.
- `node-state-reporting`: nodeagent admission control SHALL source requirements from the received `Task.requirements`, treating absence as empty requirements.

## Impact

- **Protos**: `api/core/task/task.proto` (+field), `api/core/node/capabilities.proto` (−2 fields, comment updates).
- **Gateway**: `src/core/gateway/gateway_http_handler.cc` — wire resolved requirements into the outgoing `Task`; `ExactStateTracker::RecordSubmission` consumes the same object.
- **Nodeagent**: `src/core/nodeagent/nodeagent_tlv_handler.cc` (delete `resolveRequirements`, admit from task), `src/core/nodeagent/capabilities.cc` (stop copying defaults), `TaskHandlerFactory::ParseConfig` contract (returns concurrency only), echo/piped-executable factories' config parsing.
- **Config/docs**: `config/examples/*.yaml`, `docs/config.md` — drop `default_resources` blocks.
- **Tests**: `test/core/config/config_loader_test.cc`, `test/core/nodeagent/nodeagent_capabilities_test.cc`, `test/core/nodeagent/nodeagent_tlv_handler_test.cc` (defaults-declared scenarios become requirements-carried-on-task scenarios), plus round-trip coverage for the new field.
- **ROADMAP.md**: mark the `default_resources` × repo-resolved-requirements open question as resolved.
