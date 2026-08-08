## ADDED Requirements

### Requirement: HTTP headers with the x-strij- prefix become task parameters
The gateway SHALL forward request headers whose names start with `x-strij-` into the `Task.parameters` map: the `x-strij-` prefix SHALL be stripped, the remainder SHALL be lowercased and used as the parameter key, and the header value SHALL become the parameter value. Header names SHALL be matched case-insensitively. All other request headers SHALL NOT be forwarded.

#### Scenario: x-strij header maps to a parameter
- **WHEN** a request header `x-strij-function: /usr/bin/cat` is received
- **THEN** `Task.parameters["function"]` SHALL equal "/usr/bin/cat"

#### Scenario: Header name matching is case-insensitive
- **WHEN** a request header `X-STRIJ-Function: /usr/bin/cat` is received
- **THEN** `Task.parameters["function"]` SHALL equal "/usr/bin/cat"

#### Scenario: Non-prefixed headers are not forwarded
- **WHEN** a request carries `host: example.com` and `authorization: Bearer xyz`
- **THEN** the resulting `Task.parameters` SHALL be empty

### Requirement: function is a well-known task parameter key
The `function` parameter key SHALL identify the executable a task wants to run. All function-consuming task handlers SHALL read it via a shared constant. It SHALL be populated from the `x-strij-function` request header.

#### Scenario: function key is populated from the header
- **WHEN** a request header `x-strij-function: /usr/bin/cat` is received
- **THEN** `Task.parameters` SHALL contain key "function" with value "/usr/bin/cat"

#### Scenario: Handler reads the key via the shared constant
- **WHEN** a task handler accesses the shared function parameter constant
- **THEN** the constant SHALL equal "function"
