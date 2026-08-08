# Dispatcher Connect

## Purpose

Extends the `Dispatcher` with an async connect operation, allowing `Completable`-implementing objects to submit non-blocking socket connect via io_uring.

## Requirements

### Requirement: Dispatcher provides PrepareConnect
`Dispatcher` SHALL provide a `PrepareConnect(Completable* io, uint8_t tag, int fd, const struct sockaddr* addr, socklen_t addrlen)` method that submits an async connect operation to the io_uring ring. The operation SHALL deliver a completion to `io->HandleCompletion(tag, res, flags)` where `res` is 0 on success or a negative errno on failure.

#### Scenario: Successful async connect
- **WHEN** `Dispatcher::PrepareConnect(io, tag, fd, addr, addrlen)` is called
- **AND** the connect completes successfully
- **THEN** `io->HandleCompletion(tag, 0, 0)` SHALL be called

#### Scenario: Failed async connect
- **WHEN** `Dispatcher::PrepareConnect(io, tag, fd, addr, addrlen)` is called
- **AND** the connect fails
- **THEN** `io->HandleCompletion(tag, negative_res, 0)` SHALL be called with the errno value

#### Scenario: PrepareConnect follows existing Dispatcher pattern
- **WHEN** `PrepareConnect` is implemented in `DispatcherImpl`
- **THEN** it SHALL obtain an SQE via `io_uring_get_sqe`, set user data via `io_uring_sqe_set_data` with the `Completable` and tag merged, and call `io_uring_prep_connect`

### Requirement: Dispatcher provides PreparePoll
`Dispatcher` SHALL provide a `PreparePoll(Completable* io, uint8_t tag, int fd, uint32_t poll_mask)` method that submits an async poll operation (io_uring `poll_add`) to the ring. The operation SHALL deliver a completion to `io->HandleCompletion(tag, res, flags)` when the fd becomes ready or when it is closed.

#### Scenario: Poll fires when the fd becomes ready
- **WHEN** `PreparePoll(io, tag, pidfd, POLLIN)` is called and the process exits
- **THEN** `io->HandleCompletion(tag, res, 0)` SHALL be called with `res` set to the ready mask containing `POLLIN`

#### Scenario: PreparePoll follows existing Dispatcher pattern
- **WHEN** `PreparePoll` is implemented in `DispatcherImpl`
- **THEN** it SHALL obtain an SQE via `io_uring_get_sqe`, set user data via `io_uring_sqe_set_data` with the `Completable` and tag merged, and call `io_uring_prep_poll_add`
