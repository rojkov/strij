# Dispatcher Connect

## Purpose

Extends the `Dispatcher` with an async connect operation, allowing IOObjects to submit non-blocking socket connect via io_uring.

## Requirements

### Requirement: Dispatcher provides PrepareConnect
`Dispatcher` SHALL provide a `PrepareConnect(IOObject* io_object, uint8_t tag, int fd, const struct sockaddr* addr, socklen_t addrlen)` method that submits an async connect operation to the io_uring ring. The operation SHALL deliver a completion to `io_object->HandleCompletion(tag, res, flags)` where `res` is 0 on success or a negative errno on failure.

#### Scenario: Successful async connect
- **WHEN** `Dispatcher::PrepareConnect(io_object, tag, fd, addr, addrlen)` is called
- **AND** the connect completes successfully
- **THEN** `io_object->HandleCompletion(tag, 0, 0)` SHALL be called

#### Scenario: Failed async connect
- **WHEN** `Dispatcher::PrepareConnect(io_object, tag, fd, addr, addrlen)` is called
- **AND** the connect fails
- **THEN** `io_object->HandleCompletion(tag, negative_res, 0)` SHALL be called with the errno value

#### Scenario: PrepareConnect follows existing Dispatcher pattern
- **WHEN** `PrepareConnect` is implemented in `DispatcherImpl`
- **THEN** it SHALL obtain an SQE via `io_uring_get_sqe`, set user data via `io_uring_sqe_set_data` with the IOObject and tag merged, and call `io_uring_prep_connect`
