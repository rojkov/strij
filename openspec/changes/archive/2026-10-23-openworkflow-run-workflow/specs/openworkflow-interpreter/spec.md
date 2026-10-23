# Spec Delta

## ADDED Requirements

### Requirement: A run task executes an embedded workflow process

A `run` task whose body declares the `workflow` process SHALL execute the embedded child workflow. The `container`, `shell`, and `script` process kinds SHALL remain unsupported: a document declaring one SHALL fail before execution as unimplemented.

For the workflow process, `namespace` SHALL equal `embedded`. Any other value SHALL fail before execution as unimplemented, so external resolution can be added later without changing the document shape. `input` SHALL be present and SHALL be a string containing the child workflow document; a missing or non-string `input` SHALL be a malformed document. `name` and `version` SHALL be inert for selection and matching; they MAY be used to label diagnostics. A declared `return` SHALL be rejected as unimplemented.

The child's invocation input SHALL be the run step's transformed task input. The child SHALL start with an empty `$context` and its own data chain; it SHALL NOT inherit the parent's context. The child's final value SHALL become the step's raw output and SHALL then pass through the ordinary `output.as`, `export.as`, and `then` pipeline.

A `run` task SHALL be executable wherever a `call` task is executable, including as a sequence step, a `for` body task, and a `fork` branch task, and SHALL participate in those scopes' pipelines.

#### Scenario: An embedded workflow's output feeds the next step

- **WHEN** a run step executes an embedded workflow and the child completes with a final value
- **THEN** that value is the run step's raw output and the following step receives it as its raw input

#### Scenario: The child receives the transformed task input

- **WHEN** a run step declares `input.from: '${ .order }'`
- **THEN** the embedded child's invocation input is the JSON-serialized order, not the parent's untransformed data

#### Scenario: Non-workflow run processes remain unsupported

- **WHEN** a `run` task declares `container`, `shell`, or `script`
- **THEN** the workflow fails before execution with an unimplemented status and no child runs

#### Scenario: An external namespace remains unsupported

- **WHEN** a `run.workflow` declares `namespace: production`
- **THEN** the workflow fails before execution with an unimplemented status and no child runs

#### Scenario: Missing or non-string embedded input is malformed

- **WHEN** a `run.workflow` with `namespace: embedded` omits `input` or declares it as a mapping, array, number, or boolean
- **THEN** the workflow fails before execution with a malformed-document status

#### Scenario: Embedded name and version are inert

- **WHEN** a run step declares a `name` and `version` that differ from the embedded document's own `document.name` and `document.version`
- **THEN** execution is unaffected and the embedded document still runs

#### Scenario: A declared return is unsupported

- **WHEN** a `run.workflow` declares `return`
- **THEN** the workflow fails before execution with an unimplemented status and no child runs

#### Scenario: A run task works as a fork branch

- **WHEN** a fork branch declares a `run.workflow` task
- **THEN** the subflow runs on that branch's chain and its observed output contributes to the join like a call or `do`

### Requirement: await controls whether the run step waits for the embedded workflow

When `await` is absent or true, the run step SHALL suspend until the child reaches a terminal outcome. The child's final value SHALL be the step's raw output. A child failure SHALL fail the run step with the child's status and a message naming the run step, and no following step SHALL begin.

When `await` is false, the run step SHALL NOT suspend. After the child has started, the step SHALL complete with its transformed task input as its raw output, regardless of the child's eventual result. A child outcome after a successful start SHALL NOT affect the parent workflow and SHALL NOT be reported. A failure that prevents the child from starting SHALL fail the run step as an internal failure.

The step's `output.as`, `export.as`, and `then` SHALL apply in both modes using the raw output defined above. A non-awaited run step SHALL count as an ordinary synchronous transition for the sequence's synchronous bound.

#### Scenario: An awaited subflow delays the following step

- **WHEN** a run step awaits an embedded workflow that has not completed
- **THEN** no following step begins until the child's terminal outcome arrives, and the following step then receives the child's final value

#### Scenario: An awaited child failure propagates

- **WHEN** an awaited embedded workflow fails with a status
- **THEN** the run step fails with that status and a message naming the run step, and no following step runs

#### Scenario: A non-awaited subflow completes the step immediately

- **WHEN** a run step declares `await: false`
- **THEN** the step completes with its transformed task input as its raw output without waiting for the subflow

#### Scenario: A non-awaited subflow's later failure is ignored

- **WHEN** a non-awaited embedded workflow fails after it started
- **THEN** the parent workflow is unaffected and no failure is reported for it

#### Scenario: Output shaping applies to the non-awaited step's transformed input

- **WHEN** a non-awaited run step declares `output.as: '${ { seen: . } }'`
- **THEN** the following step receives the transformed task input wrapped in the shaped object, not the subflow's result

#### Scenario: An omitted await waits

- **WHEN** a run step omits `await`
- **THEN** it behaves as `await: true`

### Requirement: Embedded subflow definitions are validated before execution

The pre-execution validation pass SHALL, for every `run.workflow` step, parse the embedded document, check its expression evaluation mode, and recursively validate that document and every workflow it embeds. A failure SHALL be reported before the parent's first step runs and before any child is submitted, and SHALL name the parent step and the embedded workflow's declared `name`.

The pass SHALL apply whether or not the step's `if` would skip it and whether or not it awaits. A malformed embedded document, a non-string `input`, an unsupported evaluation mode, or an unsupported property SHALL use the corresponding malformed or unimplemented status.

Recursive embedding SHALL be bounded by an implementation-defined nesting limit. Exceeding it SHALL fail the document before execution as unimplemented, naming the step. A document that passes this pass SHALL NOT fail later because of its own syntax, validation, or evaluation mode.

#### Scenario: A malformed embedded document fails before any step

- **WHEN** a `run.workflow` input string is not a valid workflow document
- **THEN** the parent fails in the validation pass with a malformed-document status and no step runs

#### Scenario: An unsupported embedded evaluation mode fails before execution

- **WHEN** the embedded document declares `evaluate.mode: loose`
- **THEN** the parent fails in the validation pass before any step runs

#### Scenario: A malformed grandchild fails before execution

- **WHEN** an embedded document itself embeds a malformed workflow
- **THEN** the parent fails in the validation pass before any step runs

#### Scenario: Skipped or non-awaited steps are still validated

- **WHEN** a run step declares `if: '${ false }'` or `await: false` and its embedded document is malformed
- **THEN** the parent still fails in the validation pass before any step runs

#### Scenario: Excessive embedded nesting fails before execution

- **WHEN** embedded workflows nest beyond the implementation-defined limit
- **THEN** the parent fails in the validation pass with an unimplemented status, naming the step

#### Scenario: A valid embedded document passes validation

- **WHEN** an embedded document and all of its embedded children are valid and use strict mode
- **THEN** validation succeeds and execution may proceed

### Requirement: Awaited subflows follow the parent lifecycle; non-awaited subflows are detached

An awaited nested run SHALL be part of the parent's execution lifecycle. Cancelling the parent run, including because the originating connection is torn down, SHALL cancel its outstanding awaited nested runs, and no further result SHALL be delivered for them. Abandoning a frame through flow control SHALL drop that frame's outstanding outcomes as for other child work.

A non-awaited nested run SHALL be detached from the parent's lifecycle. It SHALL continue after the parent completes, ends, or is cancelled, and its eventual outcome SHALL be discarded. A detached run SHALL NOT cause a result to be delivered to the originating connection.

#### Scenario: Cancelling the parent cancels an awaited subflow

- **WHEN** the parent run is cancelled while an awaited subflow is outstanding
- **THEN** the subflow is cancelled and no further result is delivered for it

#### Scenario: A detached subflow survives the parent

- **WHEN** the parent completes or its connection closes while a non-awaited subflow is outstanding
- **THEN** the subflow continues and its eventual outcome produces no parent result

#### Scenario: An abandoned branch drops an awaited outcome

- **WHEN** a competing fork resolves and abandons a branch awaiting a subflow
- **THEN** that branch's eventual outcome is dropped without affecting the workflow
