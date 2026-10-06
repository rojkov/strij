# Spec Delta

## MODIFIED Requirements

### Requirement: HttpResultReceiver frames responses by first-result finality

The HTTP response framing SHALL be decided once at the first delivered result. If the first result is final, the receiver SHALL write a single HTTP response with `Content-Length` equal to the result body size followed by the body, and SHALL NOT use chunked encoding. If the first result is not final, the receiver SHALL write a response with `Transfer-Encoding: chunked`, SHALL write each result as a chunk frame (`{hex-size}\r\n{data}\r\n`), and SHALL write the terminal `0\r\n\r\n` chunk on the final result.

The HTTP status written SHALL be 200 for a successful outcome and the status mapped from the failure's code for a failed one. A failure SHALL be reported on the first result only, because the status line is fixed at that point; a failure reported on a later result SHALL NOT change the status already written, and SHALL be ignored and logged as unexpected.

#### Scenario: Single-shot result uses Content-Length

- **WHEN** `Deliver(body, true)` is called once on a fresh receiver
- **THEN** the receiver SHALL write "HTTP/1.1 200 OK" with `Content-Length` equal to the body size and the body
- **AND** SHALL NOT use `Transfer-Encoding: chunked`

#### Scenario: Streaming results use chunked encoding

- **WHEN** `Deliver("a", false)`, `Deliver("b", false)`, and `Deliver("c", true)` are called in order
- **THEN** the receiver SHALL write the status line with `Transfer-Encoding: chunked`
- **AND** SHALL write a chunk frame for each of "a", "b", and "c"
- **AND** SHALL write the terminal chunk after "c"

#### Scenario: A successful first result writes 200

- **WHEN** the first result of a task is a success
- **THEN** the receiver SHALL write "HTTP/1.1 200 OK" as the status line

#### Scenario: A failed first result writes the mapped status

- **WHEN** the first result of a task carries a failure code
- **THEN** the receiver SHALL write the HTTP status mapped from that code rather than 200
- **AND** SHALL carry the failure message as the response body

#### Scenario: Two different failures are distinguishable

- **WHEN** one request fails for a malformed-request reason and another fails for a capacity reason
- **THEN** the two responses SHALL carry different HTTP statuses

#### Scenario: A failure after streaming has begun does not alter the status

- **WHEN** a task's first result was a success and started a chunked response, and a later result carries a failure
- **THEN** the receiver SHALL keep the status line it already wrote
- **AND** SHALL log the failure as unexpected

#### Scenario: An unrecognized code still yields a server error

- **WHEN** a failure carries a code the receiver does not recognize
- **THEN** the receiver SHALL write the HTTP status designated for an internal failure
- **AND** SHALL NOT write 200
