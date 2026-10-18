# Spec Delta

## MODIFIED Requirements

### Requirement: function is a well-known task parameter key

The `function` parameter key SHALL identify the function artifact a task's handler consumes. The artifact MAY be an executable, a workflow document, or another kind of function definition. All function-consuming task handlers SHALL read it via a shared constant. It SHALL be populated from the `x-strij-function` request header.

#### Scenario: function key is populated from the header

- **WHEN** a request header `x-strij-function: /usr/bin/cat` is received
- **THEN** `Task.parameters` SHALL contain key "function" with value "/usr/bin/cat"

#### Scenario: A workflow document reference is accepted

- **WHEN** a request header `x-strij-function: /tmp/workflow.yaml` is received
- **THEN** `Task.parameters["function"]` SHALL equal "/tmp/workflow.yaml"

#### Scenario: Handler reads the key via the shared constant

- **WHEN** a task handler accesses the shared function parameter constant
- **THEN** the constant SHALL equal "function"
