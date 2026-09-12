# nodeagent-task-handlers

## ADDED Requirements

### Requirement: TaskHandlerFactory may access the child-task submitter at construction

`TaskHandlerFactory::Create(config, context)` SHALL be able to retrieve a `ChildTaskSubmitter` from the `NodeagentFactoryContext` in order to pass it to a handler that submits child tasks. Only such handlers SHALL request it; the `TaskHandler` interface (`HandleTask(const task::Task&, ResultSenderPtr)`) and the sender-only constructor shape of existing handlers SHALL remain unchanged. A handler that constructs with the submitter SHALL store it as an immutable constructor dependency for the handler's lifetime.

#### Scenario: Workflow handler factory wires the submitter

- **WHEN** a workflow handler factory's `Create(config, context)` retrieves `context.ChildTaskSubmitter()`
- **THEN** the returned submitter SHALL be passed to the handler's constructor and retained by the handler

#### Scenario: Non-submitting handlers never see the submitter

- **WHEN** the `echo` and `piped_executable` factories call `Create`
- **THEN** their handlers SHALL be constructed exactly as before, with no submitter parameter

### Requirement: Sender-only handlers remain the baseline

The nodeagent SHALL continue to treat the sender-only `HandleTask` contract as the baseline for task handlers. Handlers that do not submit children SHALL not require any change to signatures, constructors, or initialization, and their existing tests SHALL remain valid without modification.

#### Scenario: Echo handler behavior is unchanged

- **WHEN** the echo handler processes a task
- **THEN** it SHALL deliver a `TaskResult` with the same id, same body, and `is_final = true` via the provided sender, exactly as before