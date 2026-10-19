# Spec Delta

## MODIFIED Requirements

### Requirement: A workflow document is executed as a resumable step sequence

The system SHALL execute a workflow document's top-level `do` body as an ordered sequence of tasks, advancing one task at a time. Nested `do` bodies and `fork` branches SHALL execute as nested sequences at the position of the task that declares them. A step that must await a result SHALL suspend its sequence and the workflow SHALL resume within that sequence once that result arrives. Steps belonging to different `fork` branches SHALL be able to await their results concurrently. The system SHALL NOT require the whole body to complete within the call that started the workflow.

#### Scenario: Body runs in declaration order

- **WHEN** a workflow body declares three steps in order and each completes without suspending on external work
- **THEN** the steps execute in declaration order and the workflow completes after the third

#### Scenario: Execution suspends across a child result

- **WHEN** a step submits a child task and suspends awaiting its result
- **THEN** no further step begins until that result arrives, and execution resumes at the step following the suspended one

#### Scenario: Local child completing during submission does not deadlock

- **WHEN** a step's child task is admitted locally and its result is delivered before the submission call returns
- **THEN** the step records that result and execution continues without waiting for a second delivery

#### Scenario: Nested do body executes at its position

- **WHEN** a task in the body declares a nested `do` sequence
- **THEN** that sequence's steps execute at the position of the nested task, and the step following the nested task runs after the nested sequence completes

#### Scenario: A fork suspends its enclosing sequence

- **WHEN** a task declares a fork whose branches are still outstanding
- **THEN** no task following the fork in the enclosing sequence begins until the fork's join has resolved

### Requirement: Validation that does not depend on data completes before any child is submitted

The system SHALL perform a validation pass over the document before executing its first step. That pass SHALL cover the facts derivable from the document alone: the call target of each call, the key set of each call's arguments, the value kind of each declared requirement, and the syntax of every runtime expression. The pass SHALL cover every task in every nested sequence, including the branch tasks of every fork. A fork declared to compete with no branches SHALL be rejected in this pass. A failure in this pass SHALL be reported before any child task is submitted. Failures that depend on data SHALL instead occur during execution.

#### Scenario: Malformed document fails before submitting anything

- **WHEN** a document declares an unknown call target, an unknown key in a call's arguments, a non-integer requirement, or an invalid expression
- **THEN** the workflow fails with that error and no child task has been submitted

#### Scenario: Data-dependent failure occurs during execution

- **WHEN** a document is structurally valid but a step's expression is given a non-JSON subject at run time
- **THEN** the failure occurs during execution rather than in the pre-execution pass

#### Scenario: Validation errors are distinguishable from run errors

- **WHEN** either kind of failure occurs
- **THEN** the reported error indicates whether the document is malformed or the run was given the wrong data

#### Scenario: Malformed task inside a fork branch fails before submitting anything

- **WHEN** a fork branch declares a task with an unknown call target or an invalid expression
- **THEN** the workflow fails with that error during the validation pass and no child task has been submitted

#### Scenario: A competing fork without branches is malformed

- **WHEN** a document declares a fork with `compete: true` and no branches
- **THEN** the workflow fails in the validation pass with a malformed-document error and no child task has been submitted

## ADDED Requirements

### Requirement: A fork task executes its branches as concurrent nested sequences

A fork task's branches SHALL execute as nested ordered sequences. Each branch SHALL begin with a copy of the data value current at the fork step, and that branch's steps SHALL carry values along this copied chain. A branch task MAY itself be a call, a do, or another fork; any other task kind inside a fork SHALL be reported the same way it is outside a fork. The sequence containing the fork SHALL remain suspended until the fork's join has resolved.

#### Scenario: Branches advance concurrently

- **WHEN** a fork declares three branches and each branch's first step awaits a child result
- **THEN** all three children are submitted before the fork resolves, and each branch's steps advance on that branch's own chain

#### Scenario: A multi-step branch keeps its own chain

- **WHEN** a branch declares two sequential calls
- **THEN** the second call's expression subject is the first call's result within that branch, independent of the other branches' results

#### Scenario: A nested fork's join is its branch's result

- **WHEN** a branch declares a fork task
- **THEN** the inner fork's join value becomes the value that branch contributes to the outer join

### Requirement: The fork join produces the value that continues the data chain

When the fork does not set `compete`, or sets it to false, the join value SHALL be a JSON array of every branch's result in declaration order. When the fork sets `compete: true`, the join value SHALL be the result of the first branch to reach a terminal outcome; outcomes of the remaining branches arriving after that point SHALL be ignored. Under join-all, a branch result that is not a JSON value SHALL fail the workflow at the join site with a failure naming that branch; the system SHALL NOT invent a representation for opaque branch results.

#### Scenario: Join-all yields an ordered array

- **WHEN** a fork with three branches completes all of them and the document does not set `compete`
- **THEN** the join value, and the expression subject of the task following the fork, is a JSON array of the three branch results in declaration order

#### Scenario: Compete yields the winner's result

- **WHEN** a fork with `compete: true` completes one branch while others are still running
- **THEN** the join value is that branch's result, and the task following the fork receives it as its data value

#### Scenario: Late outcomes after the winner are ignored

- **WHEN** a fork with `compete: true` has resolved on one branch's outcome and another branch's outcome arrives afterwards
- **THEN** the late outcome changes nothing: the join value and the workflow's course remain as decided

#### Scenario: An opaque branch result fails the join

- **WHEN** every branch of a join-all fork completes, and one branch's result is not JSON
- **THEN** the workflow fails at the join with a failure naming that branch, rather than producing an array

### Requirement: A fork failure ends the workflow immediately

The first terminal branch outcome that is a failure SHALL fail the workflow with that failure's status and a message naming the branch, and the remaining branches SHALL be abandoned. This applies identically under join-all and under `compete: true`; under `compete: true` the first terminal outcome — success or failure — is what decides the fork.

#### Scenario: Join-all fails fast on one branch failure

- **WHEN** a join-all fork has one branch fail while sibling branches are still running
- **THEN** the workflow fails with that branch's status, naming the branch, without waiting for the siblings to finish

#### Scenario: Compete can be decided by a failure

- **WHEN** a fork with `compete: true` has its first terminal outcome be a branch failure, even though another branch might succeed later
- **THEN** the workflow fails with that branch's status, naming the branch
