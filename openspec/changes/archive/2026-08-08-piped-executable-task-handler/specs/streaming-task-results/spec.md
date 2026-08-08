## ADDED Requirements

### Requirement: HttpResultReceiver frames responses by first-result finality
The HTTP response framing SHALL be decided once at the first delivered result. If the first result is final, the receiver SHALL write a single HTTP response with `Content-Length` equal to the result body size followed by the body, and SHALL NOT use chunked encoding. If the first result is not final, the receiver SHALL write a response with `Transfer-Encoding: chunked`, SHALL write each result as a chunk frame (`{hex-size}\r\n{data}\r\n`), and SHALL write the terminal `0\r\n\r\n` chunk on the final result.

#### Scenario: Single-shot result uses Content-Length
- **WHEN** `Deliver(body, true)` is called once on a fresh receiver
- **THEN** the receiver SHALL write "HTTP/1.1 200 OK" with `Content-Length` equal to the body size and the body
- **AND** SHALL NOT use `Transfer-Encoding: chunked`

#### Scenario: Streaming results use chunked encoding
- **WHEN** `Deliver("a", false)`, `Deliver("b", false)`, and `Deliver("c", true)` are called in order
- **THEN** the receiver SHALL write the status line with `Transfer-Encoding: chunked`
- **AND** SHALL write a chunk frame for each of "a", "b", and "c"
- **AND** SHALL write the terminal chunk after "c"

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
