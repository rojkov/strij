# streaming-task-results

## Purpose

Defines how task results can be streamed back to HTTP clients: multiple `TaskResult` chunks per task, chunked HTTP framing, and the finality rule governing how the gateway treats results with or without the `is_final` field.

## Requirements

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

### Requirement: Multi-chunk task results
A task handler SHALL be able to deliver a task result as multiple `TaskResult` messages: intermediate chunks with `is_final=false` and a single final chunk with `is_final=true`. The gateway SHALL forward each chunk to the receiver in order.

#### Scenario: Streaming chunks delivered in order
- **WHEN** a handler sends three results for one task (two non-final, then one final)
- **THEN** the gateway SHALL deliver the three chunks to the receiver in order
- **AND** SHALL mark only the last chunk as final

### Requirement: Result finality rule
Absence of the `is_final` field SHALL be treated as final. Consumers SHALL compute finality with `!has_is_final() || is_final()`.

#### Scenario: Absent is_final is treated as final
- **WHEN** a `TaskResult` without `is_final` set arrives at the gateway
- **THEN** the gateway SHALL treat it as final and remove the receiver from storage
