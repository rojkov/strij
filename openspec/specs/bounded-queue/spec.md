# bounded-queue

## Purpose

Defines the reusable `BoundedQueue<T>` primitive: a single-threaded FIFO queue with a maximum size, used by nodeagent local schedulers to hold deferred-admission work (probes/queued tasks) and by the Phase 2 probe admission path to reject incoming probes when full.

## Requirements

### Requirement: BoundedQueue is a fixed-capacity FIFO

The system SHALL provide a `BoundedQueue<T>` template in `strij::utils` implemented over a `std::deque` with a caller-specified maximum size. `Push(item)` SHALL append `item` to the back and return `true` when the queue is not full; SHALL return `false` without modifying the queue when it is full. `TryPop()` SHALL return the front element and remove it when the queue is non-empty; SHALL return empty (disengaged) when the queue is empty. The queue SHALL return `Size()`, `Empty()`, `Full()`, and `MaxSize()`, and SHALL support `Clear()`.

#### Scenario: Push accepts items up to capacity

- **WHEN** a `BoundedQueue<int>` with max size 3 has 0 items and `Push` is called with values 1, 2, and 3
- **THEN** each `Push` SHALL return `true`
- **AND** `Size()` SHALL be 3 and `Full()` SHALL be true

#### Scenario: Full queue rejects Push

- **WHEN** a `BoundedQueue<int>` with max size 2 is full (2 items) and `Push(4)` is attempted
- **THEN** `Push` SHALL return `false`
- **AND** the queue contents SHALL be unchanged and `Size()` SHALL remain 2

#### Scenario: TryPop drains in FIFO order

- **WHEN** values 1, 2, 3 are pushed in order and `TryPop()` is called three times
- **THEN** the popped values SHALL be 1, 2, 3 in that order
- **AND** after the third pop `Empty()` SHALL be true

#### Scenario: TryPop on an empty queue is disengaged

- **WHEN** `TryPop()` is called on an empty `BoundedQueue`
- **THEN** it SHALL return an empty (disengaged) value and not throw

### Requirement: BoundedQueue is single-threaded by contract

A `BoundedQueue` SHALL be owned and used by one event-loop context (a local scheduler); it SHALL NOT synchronize internally and SHALL NOT be shared across threads. Its methods SHALL NOT allocate except as `std::deque` growth demands.

#### Scenario: Queue is mutated from a single context

- **WHEN** a scheduler pushes and pops the same `BoundedQueue` instance from the nodeagent event-loop thread
- **THEN** all operations SHALL be consistent without any internal locking