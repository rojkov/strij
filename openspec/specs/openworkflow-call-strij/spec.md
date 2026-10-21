# openworkflow-call-strij

## Purpose

Defines the `call: strij` call arm: how a workflow's call arguments are projected onto a child task, which argument positions may contain expressions, how requirement amounts are coerced, and how reserved call targets are reported when they are not yet supported.

## Requirements

### Requirement: A call's arguments are a closed projection of the child task

A `call: strij` task SHALL accept arguments under exactly three keys: `type`, `parameters`, and `requirements`. Any other key SHALL be rejected with an error naming the offending key and listing the accepted keys. The keys SHALL map onto the child task as: `type` becomes the child task's type, `parameters` becomes the child task's parameters verbatim, and `requirements` becomes the child task's declared resource requirements. Task data SHALL NOT be expressed under any of these keys; it is supplied by the step's input.

#### Scenario: Valid argument set projects onto the child task

- **WHEN** a `call: strij` task declares `type`, `parameters`, and `requirements`
- **THEN** a child task is submitted whose type, parameters, and requirements are exactly those declared

#### Scenario: Unknown argument key is rejected

- **WHEN** a `call: strij` task declares a key other than `type`, `parameters`, or `requirements`
- **THEN** the workflow fails with an error naming that key and listing the three accepted keys, and no child is submitted

#### Scenario: Parameters pass through unchanged

- **WHEN** a `call: strij` task declares `parameters` with one or more entries
- **THEN** the child task's parameters contain exactly those entries, with no entry added, removed, or renamed

#### Scenario: Missing type is rejected

- **WHEN** a `call: strij` task omits `type`
- **THEN** the workflow fails with an error stating that `type` is required

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

### Requirement: Requirement amounts are unsigned integers

A declared requirement SHALL be a non-negative integer amount, with the amount's unit implied by the requirement name rather than written in the value. The system SHALL reject an amount that is not an integer, is negative, or exceeds the range of an unsigned 64-bit integer, with an error naming the requirement and the offending value. The system SHALL NOT accept a unit suffix, and SHALL NOT silently accept a numeric prefix of a non-numeric value.

#### Scenario: Integer amounts are accepted

- **WHEN** a `requirements` entry declares a non-negative integer amount
- **THEN** the child task declares that requirement with exactly that amount

#### Scenario: Fractional amount is rejected

- **WHEN** a `requirements` amount is a floating point value
- **THEN** the workflow fails with an error naming that requirement and the value

#### Scenario: Negative amount is rejected

- **WHEN** a `requirements` amount is negative
- **THEN** the workflow fails with an error naming that requirement and the value

#### Scenario: Amount with a unit suffix is rejected

- **WHEN** a `requirements` amount is a number followed by a unit suffix, such as `100M`
- **THEN** the workflow fails with an error stating that unit suffixes are not accepted, rather than accepting the numeric prefix

#### Scenario: Amount exceeding unsigned 64-bit range is rejected

- **WHEN** a `requirements` amount is an integer larger than the unsigned 64-bit maximum
- **THEN** the workflow fails with an error naming that requirement and stating the value is out of range

#### Scenario: Unparseable amount is rejected

- **WHEN** a `requirements` amount is not numeric at all
- **THEN** the workflow fails with an error naming that requirement and the value

### Requirement: Reserved call targets are reported as unsupported, distinctly from unknown ones

The system SHALL recognize the reserved call targets `http`, `openapi`, `asyncapi`, `grpc`, and `mcp`. A workflow naming one of these SHALL fail with an error stating that the target is recognized but not yet supported. This error SHALL be distinguishable from the error for a call target that is not recognized at all, so that a misspelled target is never reported as a missing capability.

#### Scenario: Reserved target reports unsupported

- **WHEN** a call declares target `http`
- **THEN** the workflow fails with an error stating that `http` is recognized but not yet supported, and no child is submitted

#### Scenario: Every reserved target is recognized

- **WHEN** a call declares target `openapi`, `asyncapi`, `grpc`, or `mcp`
- **THEN** each is reported as recognized but not yet supported

#### Scenario: Unknown target is reported as unknown

- **WHEN** a call declares a target that is neither `strij` nor a reserved target
- **THEN** the workflow fails with an error reporting the target as unrecognized, distinguishable from the unsupported error

#### Scenario: A call to a function name is not treated as a bare target

- **WHEN** a call target names a function declared by the document
- **THEN** the target is resolved against the document's declared functions, and its absence there is reported distinctly from both the unsupported and unrecognized errors

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
