## 1. Word Lists and Random Generation Setup

- [x] 1.1 Create word list arrays for adjectives (~200 words), nouns (~500 words), and verbs (~200 words) in `src/core/utils/task_id.cc`. Use curated, simple English words without offensive connotations.
- [x] 1.2 Define thread-local `std::mt19937_64` PRNG with `std::random_device` seeding in `src/core/utils/task_id.cc`.
- [x] 1.3 Create helper function to generate 8-character lowercase alphanumeric suffixes using the thread-local PRNG.

## 2. GenerateTaskId Function Implementation

- [x] 2.1 Implement `auto GenerateTaskId() -> std::string` free function in `src/core/utils/task_id.cc` that selects random words from each list and combines them with the random suffix in format `adjective_noun_verb_xxxxxxxxxx`.
- [x] 2.2 Create header file `src/core/utils/task_id.hh` declaring `GenerateTaskId()`.
- [x] 2.3 Add BUILD.bazel entry for `task_id` library in `src/core/utils/` with appropriate dependencies.
- [x] 2.4 Write unit tests for `GenerateTaskId()` in `test/core/utils/task_id_test.cc` verifying format, randomness, and thread-safety.

## 3. Update Protobuf Schema

- [x] 3.1 Modify `api/core/task/task.proto` to change `Task.id` from `uint64` to `string`.
- [x] 3.2 Modify `api/core/task/task.proto` to change `TaskResult.id` from `uint64` to `string`.
- [x] 3.3 Regenerate protobuf files using Bazel build.

## 4. Update ResultReceiverStorage

- [x] 4.1 Modify `src/core/gateway/result_receiver_storage.hh` to change key type from `uint64_t` to `std::string` in `put()`, `get()`, and `erase()` methods.
- [x] 4.2 Update implementation in `src/core/gateway/result_receiver_storage.cc` (if exists) to use `std::string` keys.
- [x] 4.3 Update BUILD.bazel dependencies if needed.

## 5. Update Gateway Handlers

- [x] 5.1 Modify `src/core/gateway/gateway_http_handler.hh` to remove `uint64_t next_task_id_` counter.
- [x] 5.2 Modify `src/core/gateway/gateway_http_handler.cc` to call `GenerateTaskId()` instead of incrementing counter.
- [x] 5.3 Update `src/core/gateway/gateway_tlv_handler.cc` to handle `TaskResult.id` as string when looking up receivers in storage.
- [x] 5.4 Ensure all `Task` and `TaskResult` protobuf field accesses use string IDs.

## 6. Update Nodeagent Handlers

- [x] 6.1 Update `src/core/nodeagent/nodeagent_tlv_handler.cc` to handle `Task.id` as string when echoing `TaskResult.id`.

## 7. Update Tests

- [x] 7.1 Modify `test/core/gateway/gateway_test.cc` to expect string task IDs instead of uint64.
- [x] 7.2 Update test assertions to parse and verify string IDs in protobuf messages.
- [x] 7.3 Update any test that uses specific numeric ID values (e.g., task_id=42) to use string equivalents.
- [x] 7.4 Add test cases for the new `GenerateTaskId()` function verifying format and properties.

## 8. Build and Integration

- [x] 8.1 Update BUILD.bazel files to add dependencies on the new `task_id` library where needed.
- [x] 8.2 Run `make build` to verify compilation succeeds.
- [x] 8.3 Run `make test` to verify all tests pass.
- [x] 8.4 Run `make clang-tidy` to verify code style compliance.
- [x] 8.5 If errors occur, fix compilation issues in handlers, storage, or protobuf usage.

## 9. Manual Verification

- [ ] 9.1 Run gateway binary and send multiple HTTP requests to verify unique, readable task IDs are generated.
- [ ] 9.2 Verify task IDs appear in logs in the expected format (e.g., "happy_fox_runs_k7m2x9p4").
- [ ] 9.3 Verify nodeagent correctly echoes string task IDs back to gateway.
