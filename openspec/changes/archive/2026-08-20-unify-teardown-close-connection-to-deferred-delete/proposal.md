# Unify teardown: migrate CLOSE_CONNECTION onto DEFERRED_DELETE

## Goal

Remove the redundant `CLOSE_CONNECTION` command type by migrating its two call sites to the existing generic `DEFERRED_DELETE` command, then delete the enum value.

## Motivation

`CLOSE_CONNECTION` and `DEFERRED_DELETE` have identical semantics: "owner, destroy the object pointed to by `args_`." The `type_` field is redundant — the owner already knows what it owns based on `destination_` (itself). The piped_executable change (D6) introduced `DEFERRED_DELETE` and noted this migration as a follow-up.

## Scope

- Change `Connection::onEndOfStream()` to submit `DEFERRED_DELETE` instead of `CLOSE_CONNECTION`.
- Update `TcpListener::ProcessCommand()` and `Node::ProcessCommand()` to match on `DEFERRED_DELETE`.
- Remove `CLOSE_CONNECTION` from `Command::Type`.
- Update the test assertion in `connection_test.cc`.
- Update specs that reference `CLOSE_CONNECTION`.

### Out of scope

- Generic `EraseFromMap` utility (deferred until more call sites appear).
- `Connection::Close()` remains unchanged (it never submitted `CLOSE_CONNECTION`; it skips the command by design).

## Impact

- **Modified:** `include/strij/event/command.hh`, `src/core/io/connection.cc`, `src/core/io/tcp_listener.cc`, `src/core/gateway/node.cc`, `test/core/io/connection_test.cc`.
- **Docs/specs:** `openspec/specs/connection-completable/spec.md`, `openspec/specs/node-directory/spec.md`.
