## MODIFIED Requirements

### Requirement: Connection handles a write completion
`Connection` SHALL handle partial writes. When `HandleCompletion(tag=Write, res=N)` is called with N > 0 and N < remaining buffer size, Connection SHALL advance its write cursor and resubmit the remaining bytes via `Dispatcher::PrepareWrite`. When the write is complete (N >= remaining) or on error, Connection SHALL clear the write buffer and reset the cursor.

#### Scenario: Connection handles a complete write
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res >= remaining bytes in write_buf_
- **THEN** Connection SHALL clear `write_buf_` and reset `write_offset_` to 0

#### Scenario: Connection handles a partial write
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with 0 < res < remaining bytes in write_buf_
- **THEN** Connection SHALL advance `write_offset_` by res and resubmit a PrepareWrite for the remaining bytes starting at write_buf_[write_offset_]

#### Scenario: Connection handles a write error
- **WHEN** the Dispatcher calls `Connection::HandleCompletion(tag=Write, res)` with res <= 0
- **THEN** Connection SHALL clear `write_buf_`, reset `write_offset_` to 0, and log the error

### Requirement: Connection owns the write buffer
`Connection` SHALL own a `std::vector<std::byte> write_buf_` member field and a `size_t write_offset_` member field. `Connection::Write(data)` SHALL copy `data` into `write_buf_`, set `write_offset_` to 0, and register an async write with the Dispatcher. The buffer SHALL remain valid until the write completes or is aborted.

#### Scenario: Handler writes a response
- **WHEN** a handler calls `conn.Write(data)`
- **THEN** Connection SHALL copy `data` into `write_buf_`, reset `write_offset_` to 0, and register an async write with the Dispatcher

#### Scenario: Write buffer lifetime
- **WHEN** `Connection` calls `Dispatcher::PrepareWrite(this, WriteTag, fd_, remaining bytes from write_offset_, 0)`
- **THEN** `write_buf_` SHALL remain valid and `write_offset_` SHALL not be modified until the write `HandleCompletion` returns

#### Scenario: Write rejects concurrent writes
- **WHEN** `Connection::Write(data)` is called while `write_buf_` is not empty (previous write in-flight)
- **THEN** Connection SHALL assert or otherwise signal a programming error
