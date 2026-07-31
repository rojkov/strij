## Why

Task IDs are currently monotonically incrementing integers, which works for a single gateway instance but produces collisions when multiple gateways run concurrently. A distributed, human-friendly task ID scheme is needed to support multi-instance deployments without external coordination.

## What Changes

- Replace `uint64_t next_task_id_` counter with a `GenerateTaskId()` function that produces unique identifiers in the format `adjective_noun_verb_xxxxxxxxxx` (e.g., `happy_fox_runs_k7m2x9p4`).
- **BREAKING**: Change protobuf schema `Task.id` and `TaskResult.id` from `uint64` to `string`.
- **BREAKING**: Update `ResultReceiverStorage` to use `std::string` keys instead of `uint64_t`.
- Update `GatewayHttpHandler` and `GatewayTlvHandler` to use string task IDs.
- Add custom word lists (200 adjectives, 500 nouns, 200 verbs) and thread-local random state for generation.

## Capabilities

### New Capabilities

- `readable-task-id-generation`: Function that generates human-friendly, collision-resistant task IDs using random words and alphanumeric suffixes.

### Modified Capabilities

- `gateway-task-bridge`: Task ID generation and storage change from `uint64_t` to `std::string` keys.
- `task-protocol`: Protobuf schema changes `id` field type from `uint64` to `string` in `Task` and `TaskResult` messages.

## Impact

- **Protobuf wire format**: Breaking change to `Task` and `TaskResult` messages. All gateway and nodeagent instances must be upgraded together or use a rolling deployment strategy.
- **Storage**: `ResultReceiverStorage` now uses `std::string` keys, requiring changes to `put()`, `get()`, and `erase()` methods.
- **Wire protocol**: TLV frames for task submission and results carry serialized protobuf with string IDs instead of uint64.
- **Dependencies**: No new external dependencies. Uses C++23 standard library (`<random>`, `<string>`, `<span>`).
- **Testing**: Existing `gateway_test.cc` assertions must be updated to expect string IDs.
