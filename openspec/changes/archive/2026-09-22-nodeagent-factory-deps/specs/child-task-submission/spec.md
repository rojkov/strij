# Spec Delta

## MODIFIED Requirements

### Requirement: ChildTaskSubmitter is a shared node service

The nodeagent SHALL provide a `ChildTaskSubmitter` service exposed through the `TaskHandlerDeps` bundle. It SHALL expose a single submission method `Submit(const task::Task& child, gateway::ResultReceiverPtr receiver)`, fire-and-forget, transferring receiver ownership. Every task handed to `Submit` SHALL eventually resolve through the receiver (result or error) — the receiver never hangs. The submitter is node-global and shared by every workflow task handler; handlers hold it as a constructor dependency, never as a per-call argument. The bundle's submitter SHALL be valid whenever a task handler factory's `Create` is invoked.

#### Scenario: Handler obtains the submitter at construction

- **WHEN** a task handler factory's `Create(config, deps)` is called and retrieves `deps.child_task_submitter_`
- **THEN** the returned reference SHALL be the node-global submitter instance shared across handler instances and connections

#### Scenario: Submit transfers receiver ownership

- **WHEN** a handler calls `Submit(child, receiver)`
- **THEN** the receiver SHALL be owned by the submission path and SHALL eventually receive a result or error
