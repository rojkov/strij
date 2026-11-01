# Spec Delta

## ADDED Requirements

### Requirement: An emit task publishes a CloudEvent

An `emit` task SHALL build a CloudEvent from its `emit.event.with` properties and publish it through the node event bus. The string context attributes (`source`, `type`, `subject`, `time`, `datacontenttype`, `dataschema`) SHALL be literal text; only the `data` value SHALL be evaluated, as a literal-capable value tree, against the transformed task input under the strict-mode expression rules. The event SHALL be completed with the event-plane defaults for missing `specversion`, `id`, `time`, and JSON `datacontenttype`. The step SHALL suspend until the event plane reports the publish complete, and SHALL then apply the ordinary `output.as`, `export.as`, and `then` pipeline. The step's raw output SHALL be its transformed task input. A publish with no available event path SHALL fail the step with an `UNAVAILABLE` status; any other publish failure SHALL fail the step with an `INTERNAL` status. An `emit` task SHALL be executable wherever a `call` task is executable, including a `for` body and a fork branch.

#### Scenario: Emit publishes the authored event

- **WHEN** an emit task declares a source, type, and data object
- **THEN** one CloudEvent with those properties plus the event-plane defaults SHALL be published
- **AND** the step SHALL complete with its transformed task input as its raw output

#### Scenario: Event data expressions use the task input

- **WHEN** an emit task declares `data: '${ { id: .orderId } }'` and the transformed task input is `{"orderId":7}`
- **THEN** the published event data SHALL be `{"id":7}`

#### Scenario: Publish failure is reported

- **WHEN** an emit task publishes while no event path is available
- **THEN** the workflow SHALL fail with an `UNAVAILABLE` status naming the emit step

#### Scenario: Emit output shaping applies

- **WHEN** an emit task declares `output.as` and `export.as`
- **THEN** they SHALL be evaluated against the transformed task input and apply before the step's `then` directive

#### Scenario: A skipped emit does not publish

- **WHEN** an emit task is skipped because its `if` condition is false
- **THEN** no event SHALL be published and the data value SHALL be unchanged

### Requirement: A listen task consumes events according to a consumption strategy

A `listen` task SHALL subscribe to the event plane with a coarse pattern derived from its `listen.to` filters, using the stable subscription identity for its workflow step activation and the `auto` delivery mode. The task SHALL suspend its sequence until the consumption strategy completes; sibling fork branches SHALL continue. The `one` strategy SHALL complete on the first event matching its single filter. The `all` strategy SHALL complete after every declared filter has been satisfied by at least one event. The `any` strategy SHALL complete on the first matching event, or, when `until` is present, SHALL continue consuming until the `until` condition or nested consumption strategy is satisfied. An `until` expression SHALL be evaluated against the accumulated consumed events. The consumed events SHALL be exposed as a JSON array in arrival order, except that `all` SHALL expose them in filter declaration order. When the task declares `output.as`, it SHALL be evaluated against that array; `export.as` and `then` SHALL apply through the ordinary pipeline. The subscription SHALL be deleted when the task completes, fails, or is abandoned.

#### Scenario: One waits for one event

- **WHEN** a `one` listen declares a filter for type `order.created`
- **THEN** the task SHALL complete on the first event of that type and SHALL expose that event as a one-element array

#### Scenario: All waits for every filter

- **WHEN** an `all` listen declares a temperature filter and a bpm filter
- **THEN** the task SHALL complete only after at least one event has satisfied each filter
- **AND** the raw output SHALL order the consumed events by filter declaration order

#### Scenario: Any completes on the first match

- **WHEN** an `any` listen has no `until` and a matching event arrives
- **THEN** the task SHALL complete with that event

#### Scenario: Any until an expression consumes until the condition

- **WHEN** an `any` listen declares `until: '${ (. | length) > 3 }'`
- **THEN** the task SHALL continue consuming and acknowledge matching events until the accumulated array contains more than three events

#### Scenario: Listen suspension is local to its branch

- **WHEN** a fork branch contains a listen task that is waiting
- **THEN** the other fork branches SHALL continue advancing

#### Scenario: Listen output shaping applies

- **WHEN** a listen task declares `output.as: '${ .data.reading }'`
- **THEN** the shaping SHALL apply to the consumed events according to the existing output rules

### Requirement: Listen filters match event properties, data, and correlations

A listen filter SHALL match an event only when every declared `with` property matches the event's context attribute and every declared `correlate` entry holds. The `with.data` value SHALL be a literal-capable value tree evaluated against the event data under strict-mode expression rules: a boolean result SHALL match when true, and a non-boolean result SHALL match when the event data contains the same structure. A correlation named `roomId` with `from: .roomid` SHALL hold when the event's `roomId` attribute equals the value at the event data path `.roomid`; a declared `expect` SHALL additionally constrain that value. An event that matches the subscription's coarse pattern but fails its workflow-level filter SHALL be acknowledged and SHALL NOT complete the strategy. A data-dependent filter failure SHALL fail the listen step with `INTERNAL` and SHALL name the listen task.

#### Scenario: Context attributes and data filter match

- **WHEN** a filter declares type `vitals.temperature` and `data: '${ .temperature > 38 }'` and the event has that type and a temperature of 39
- **THEN** the filter SHALL match

#### Scenario: A non-matching event is acknowledged

- **WHEN** an event has the subscribed type but its data predicate is false
- **THEN** the listener SHALL acknowledge the event and continue waiting

#### Scenario: Correlation matches event attribute to event data

- **WHEN** a filter correlates `roomId` from `.roomid` and the event attribute and data path are equal
- **THEN** the filter SHALL match

#### Scenario: A malformed data predicate fails the step

- **WHEN** a filter's `data` expression cannot be compiled
- **THEN** the workflow SHALL fail in the pre-execution validation pass with a malformed-request status

#### Scenario: A failing predicate at runtime fails the step

- **WHEN** a filter's data predicate fails while evaluating a delivered event
- **THEN** the workflow SHALL fail with an `INTERNAL` status naming the listen task

### Requirement: Listen foreach runs a body per consumed event

When a listen task declares `foreach`, the system SHALL run its `do` body once for each consumed event, in consumption order. Each iteration SHALL bind `foreach.item` to the selected value and `foreach.at` to the zero-based index; absent or empty names SHALL default to `item` and `index`, and a leading `$` SHALL not be part of a declared name. The selected value SHALL be the event data when `read` is `data` or absent, and the full CloudEvent envelope when `read` is `envelope`. Each iteration's body SHALL begin with the selected value as its raw input; `foreach.output.as` and `foreach.export.as` SHALL apply to that iteration. The listen task's raw output SHALL be the array of iteration results in consumption order. The declared item and index names SHALL be valid identifiers and SHALL differ from each other and from the reserved `context`, `input`, and `output` names.

#### Scenario: Foreach runs once per event

- **WHEN** an `any` listen with `foreach` consumes three events
- **THEN** the body SHALL run three times in consumption order and the listen output SHALL contain three iteration results

#### Scenario: Read data selects the payload

- **WHEN** `read` is `data` and an event carries `{"temperature":39}`
- **THEN** the iteration's selected value SHALL be `{"temperature":39}`

#### Scenario: Read envelope selects the CloudEvent

- **WHEN** `read` is `envelope` and an event is delivered
- **THEN** the iteration's selected value SHALL be the full CloudEvent envelope, including its context attributes and data

#### Scenario: Foreach variables are bound

- **WHEN** `foreach` declares `item: message` and `at: position`
- **THEN** the body SHALL see `$message` and `$position` for each consumed event

#### Scenario: Invalid foreach variables are malformed

- **WHEN** `foreach` declares equal names or a reserved name
- **THEN** the workflow SHALL fail validation with a malformed-request status before subscribing

### Requirement: Listen cancellation and delivery failure are reported

When a listen task completes, fails, or is abandoned by flow control, the system SHALL delete or cancel its subscription. When the subscription cannot be retained or reattached after a transport failure, the listen step SHALL fail with `UNAVAILABLE` rather than wait indefinitely. Cancelling the workflow SHALL cancel its outstanding listen subscriptions and SHALL NOT deliver a further result for them.

#### Scenario: Completion cancels the subscription

- **WHEN** a listen task completes its consumption strategy
- **THEN** its event subscription SHALL be cancelled, and no later event SHALL be delivered to it

#### Scenario: Abandonment cancels the subscription

- **WHEN** a competing fork resolves and abandons a branch containing a waiting listen
- **THEN** that branch's subscription SHALL be cancelled and its later events SHALL be dropped

#### Scenario: A lost best-effort subscription fails instead of hanging

- **WHEN** a best-effort listen subscription is detached and cannot be reattached
- **THEN** the listen step SHALL fail with an `UNAVAILABLE` status

#### Scenario: Workflow cancellation cancels listen subscriptions

- **WHEN** the originating connection closes while a listen is waiting
- **THEN** the workflow SHALL stop and its listen subscriptions SHALL be cancelled

### Requirement: Emit and listen are validated before any subscription or publish

The pre-execution validation pass SHALL cover every `emit` and `listen` task in every nested sequence. It SHALL compile every expression in an emit's `data` value, every listen filter's `data` value, every `until` expression, and the expression form of a `for`-like collection where applicable. It SHALL validate that a `one` strategy declares exactly one filter, an `all` strategy declares at least one filter, `read` is `data` or `envelope`, `foreach` variable names are valid and distinct, and a nested `until` strategy is structurally valid. A failure in this pass SHALL fail the workflow with a malformed-request status before any event is published or any subscription is created.

#### Scenario: Malformed emit data fails before publishing

- **WHEN** an emit task declares a `data` expression that is not a valid program
- **THEN** the workflow SHALL fail validation before publishing any event

#### Scenario: Malformed listen filter fails before subscribing

- **WHEN** a listen filter declares a malformed `data` expression
- **THEN** the workflow SHALL fail validation before creating any subscription

#### Scenario: Invalid read mode is malformed

- **WHEN** a listen task declares `read` other than `data` or `envelope`
- **THEN** the workflow SHALL fail validation with a malformed-request status

#### Scenario: Invalid strategy shape is malformed

- **WHEN** a `one` listen declares no filter, or an `all` listen declares no filters
- **THEN** the workflow SHALL fail validation with a malformed-request status

#### Scenario: A malformed listen inside a nested body fails before execution

- **WHEN** a listen task inside a `do`, `for`, or fork branch is malformed
- **THEN** the whole workflow SHALL fail validation before any step runs

## MODIFIED Requirements

### Requirement: A fork task executes its branches as concurrent nested sequences

A fork task's branches SHALL execute as nested ordered sequences. Each branch SHALL begin with a copy of the data value current at the fork step, and that branch's steps SHALL carry values along this copied chain. A branch task MAY itself be a call, a do, a for, a fork, a set, a switch, an emit, or a listen; any other task kind inside a fork SHALL be reported the same way it is outside a fork. Because a branch is a single task, a branch that needs multiple steps or a choice among alternative steps SHALL declare a `do`; a branch that is directly a `switch` SHALL resolve its case directives only within its own one-task scope. The sequence containing the fork SHALL remain suspended until the fork's join has resolved.

#### Scenario: Branches advance concurrently

- **WHEN** a fork declares three branches and each branch's first step awaits a child result
- **THEN** all three children are submitted before the fork resolves, and each branch's steps advance on that branch's own chain

#### Scenario: A multi-step branch keeps its own chain

- **WHEN** a branch declares two sequential calls
- **THEN** the second call's expression subject is the first call's result within that branch, independent of the other branches' results

#### Scenario: A nested fork's join is its branch's result

- **WHEN** a branch declares a fork task
- **THEN** the inner fork's join value becomes the value that branch contributes to the outer join

#### Scenario: A set branch runs on its branch chain

- **WHEN** a branch declares a `set` task
- **THEN** that task runs on the branch's own chain and contributes its result to the join like a call or `do`

#### Scenario: A for branch runs on its branch chain

- **WHEN** a branch declares a `for` task
- **THEN** the loop runs on the branch's own chain and its final accumulator contributes to the join like a call, `do`, or `set`

#### Scenario: An emit branch runs on its branch chain

- **WHEN** a branch declares an `emit` task
- **THEN** the event is published on that branch's chain and the branch contributes the step's transformed input to the join

#### Scenario: A listen branch waits only its own branch

- **WHEN** a branch declares a `listen` task that is waiting for an event
- **THEN** the other branches continue advancing and the fork resolves after the listen branch completes or fails

#### Scenario: A multi-step branch declares a do

- **WHEN** a branch needs to run several tasks sequentially or select among alternative steps
- **THEN** the branch task is a `do` whose body contains those tasks, and flow directives inside it resolve within that body

#### Scenario: A direct switch branch is limited to its one-task scope

- **WHEN** a fork declares a `switch` task directly as a branch
- **THEN** its case directives may only continue, exit, end, or target the switch itself; `continue` and `exit` both complete the branch, `end` concludes the workflow, and a case naming a task in another branch is malformed
