# Tasks

## 1. Frame substrate

- [x] 1.1 Define the frame tree in `interpreter.{hh,cc}`: sequence frame (task list, cursor, own data value) and fork frame (branch frames, compete flag, join state), and build it from the document (root `do`, nested `do` as nested sequence frames, `fork` as fork frames); remove `CollectSteps`. Verify: unit tests construct frames for a flat document, a nested-`do` document, and a document with a fork, and assert the tree shape and cursors
- [x] 1.2 Rework the validation pass to recurse the document structure (every task node including branch tasks) and reject `compete: true` forks with no branches; update `ValidateDocument`'s signature away from the flat vector. Verify: existing validation tests pass unchanged, plus new tests for a malformed call inside a branch failing pre-submission and an empty competing fork failing as malformed
- [x] 1.3 Rework the step loop over frames: per-frame cursor advancement, per-sequence data values, child-id → owning-frame owner map, and `pending_` as a map keyed by child id with the `in_submit_` drain preserved. Verify: all existing sequential interpreter tests pass with unchanged behavior, including the local-child-completes-inside-`Submit` test

## 2. Fork execution

- [x] 2.1 Implement fork entry: each branch chain seeds from a copy of the fork's data value; advance branches in declaration order until each suspends or completes (design D2). Verify: a three-branch fork submits all three children before the fork resolves; a local-admits-inline branch does not disturb a sibling's advancement
- [x] 2.2 Implement the join-all join (`compete` false/absent): on the last branch outcome, produce a JSON array of branch results in declaration order and write it into the parent sequence frame's data value; an opaque branch result fails the workflow at the join site naming the branch. Verify: ordered-array test, next-subject-is-array test, and opaque-branch-result failure test
- [x] 2.3 Implement the compete join (`compete: true`): the first terminal branch outcome resolves the fork with that result; remaining branches are abandoned and late outcomes are ignored (owner-map entries erased, weak receivers unchanged). Verify: winner's-result test and a late-second-outcome-no-op test
- [x] 2.4 Implement fail-fast failure propagation: the first terminal branch failure escalates through the frame chain (no interception) and fails the workflow with the child's status and a message naming the branch; siblings abandoned in both modes. Verify: join-all fail-fast test (siblings still running) and compete-decided-by-first-failure test
- [x] 2.5 Verify nested fork composition: a fork task inside a branch runs as a fork frame whose join value becomes the branch's contributed result. Verify: nested-fork unit test covering an inner compete join inside an outer join-all

## 3. Integration and checks

- [x] 3.1 Add end-to-end coverage through the task handler with real document fixtures: one join-all document and one competing document with `call: strij` branches. Verify: `openworkflow_e2e_test` scenarios pass
- [x] 3.2 Run the full verification suite. Verify: `make test` passes, `make check` (include purity, namespace coherence) passes, and `make clang-tidy` reports no new findings in the touched files
