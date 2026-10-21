# Spec Delta

## MODIFIED Requirements

### Requirement: Expression eligibility is decided per argument position

The system SHALL admit runtime expressions only in the argument positions that permit them, and SHALL recognize one only under the strict-mode rule (`openworkflow-interpreter`: a string is an expression if and only if it is wrapped in `${ }`). `type` SHALL NOT admit an expression, and a `type` written as `${ }` SHALL be rejected. `requirements` amounts SHALL NOT admit an expression, and an amount written as `${ }` SHALL be rejected. A `parameters` value SHALL admit an expression only when `${ }` is its entire value; a `parameters` value with literal text surrounding the wrapper SHALL be rejected. A value that is wholly an expression SHALL be evaluated against the task's transformed input, and the result SHALL be rendered as the parameter's string value. When the task declares no `input.from`, the transformed input is the data chain.

#### Scenario: Parameters accept a whole-value expression

- **WHEN** a `parameters` value is an expression occupying the entire value
- **THEN** the expression is evaluated with the task's transformed input as its subject and its result is rendered as the parameter's string value

#### Scenario: Parameters use input.from when declared

- **WHEN** a task declares `input.from: '${ .payload }'` and a parameter expression reads `.id`
- **THEN** the parameter expression reads `.id` from the payload rather than from the raw task input

#### Scenario: Type does not accept an expression

- **WHEN** `type` is written as `${ }`
- **THEN** the workflow fails with an error stating that `type` does not admit expressions

#### Scenario: Requirements do not accept expressions

- **WHEN** a `requirements` amount is written as `${ }`
- **THEN** the workflow fails with an error stating that requirement amounts do not admit expressions

#### Scenario: A partially-wrapped parameters value is rejected

- **WHEN** a `parameters` value has literal text outside the `${ }` wrapper, such as `"jobs/${ .id }"`
- **THEN** the workflow fails with an error stating the wrapper must span the whole value

#### Scenario: An expression producing a non-scalar is rejected

- **WHEN** a `parameters` expression evaluates to an object or an array
- **THEN** the workflow fails with an error stating that the result is not a scalar, and no child is submitted

#### Scenario: An expression producing null is rejected

- **WHEN** a `parameters` expression evaluates to null
- **THEN** the workflow fails with an error stating the result is null, and no child is submitted

#### Scenario: Rendered parameters are not JSON-quoted

- **WHEN** a `parameters` value is the literal string `/bin/task.sh`
- **THEN** the child task's parameter value is `/bin/task.sh` without surrounding quotes

## ADDED Requirements

### Requirement: The child body carries the JSON-faithful task input

A `call: strij` child task's body SHALL be the step's transformed task input. When that input is a JSON value, the body SHALL be its JSON serialization; when it is opaque bytes, the body SHALL be those bytes. A string produced by `input.from` SHALL therefore be carried as a JSON string, with quotes. When the task declares no `input.from`, the body SHALL be the raw data value unchanged.

#### Scenario: Input transformation shapes the child body

- **WHEN** a `call: strij` task declares `input.from: '${ .order }'`
- **THEN** the child task's body is the JSON serialization of the order value

#### Scenario: A string transformation is JSON-quoted

- **WHEN** `input.from` evaluates to the string `world`
- **THEN** the child task's body is `"world"`, not `world`

#### Scenario: No transformation passes the raw data value

- **WHEN** a `call: strij` task declares no `input.from`
- **THEN** the child task's body is the raw data value, including opaque bytes unchanged

#### Scenario: Opaque transformed input passes as bytes

- **WHEN** a `call: strij` task's transformed input is opaque
- **THEN** the child task's body is those bytes unchanged
