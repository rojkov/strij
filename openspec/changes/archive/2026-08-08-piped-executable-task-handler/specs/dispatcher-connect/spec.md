## ADDED Requirements

### Requirement: Dispatcher provides PreparePoll
`Dispatcher` SHALL provide a `PreparePoll(Completable* io, uint8_t tag, int fd, uint32_t poll_mask)` method that submits an async poll operation (io_uring `poll_add`) to the ring. The operation SHALL deliver a completion to `io->HandleCompletion(tag, res, flags)` when the fd becomes ready or when it is closed.

#### Scenario: Poll fires when the fd becomes ready
- **WHEN** `PreparePoll(io, tag, pidfd, POLLIN)` is called and the process exits
- **THEN** `io->HandleCompletion(tag, res, 0)` SHALL be called with `res` set to the ready mask containing `POLLIN`

#### Scenario: PreparePoll follows existing Dispatcher pattern
- **WHEN** `PreparePoll` is implemented in `DispatcherImpl`
- **THEN** it SHALL obtain an SQE via `io_uring_get_sqe`, set user data via `io_uring_sqe_set_data` with the `Completable` and tag merged, and call `io_uring_prep_poll_add`
