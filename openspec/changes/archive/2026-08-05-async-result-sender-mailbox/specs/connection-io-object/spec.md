## MODIFIED Requirements

### Requirement: Connection owns the write buffer
`Connection` SHALL own a `std::deque<std::vector<std::byte>> write_queue_` member field. `Connection::Write(data)` SHALL copy `data` into a new buffer and append it to `write_queue_`; if the queue was empty, Connection SHALL immediately register an async write of the front buffer with the Dispatcher. Each buffer SHALL remain valid until its write completes or is aborted. Concurrent `Write` calls SHALL be allowed; buffers SHALL drain in FIFO order, one async write at a time.

#### Scenario: Handler writes a response
- **WHEN** a handler calls `conn.Write(data)`
- **THEN** Connection SHALL copy `data` into a new buffer, append it to `write_queue_`, and if the queue was empty register an async write with the Dispatcher

#### Scenario: Write buffer lifetime
- **WHEN** Connection submits `Dispatcher::PrepareWrite(this, WriteTag, fd_, front buffer from write_offset_, 0)`
- **THEN** the front buffer SHALL remain valid and `write_offset_` SHALL not be modified until the write `HandleCompletion` returns

#### Scenario: Write queues concurrent writes
- **WHEN** `Connection::Write(data)` is called while a previous write is in-flight
- **THEN** the new buffer SHALL be appended to `write_queue_` and SHALL be written after the in-flight write completes

#### Scenario: Connection handles a partial write of the front buffer
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res > 0 and res < remaining bytes in the front buffer
- **THEN** Connection SHALL advance `write_offset_` by res and resubmit a PrepareWrite for the remaining bytes of the front buffer

#### Scenario: Connection handles a complete buffer and drains the queue
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res >= remaining bytes in the front buffer
- **THEN** Connection SHALL pop the front buffer, reset `write_offset_` to 0
- **AND** if `write_queue_` is non-empty, register an async write for the new front buffer

#### Scenario: Connection handles a write error
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res <= 0
- **THEN** Connection SHALL clear `write_queue_`, reset `write_offset_` to 0, and log the error
