# Design: Migrate CLOSE_CONNECTION onto DEFERRED_DELETE

## Decision

**D1. Replace `CLOSE_CONNECTION` with `DEFERRED_DELETE` everywhere.**

`Connection::onEndOfStream()` currently submits `CLOSE_CONNECTION` with `destination_=owner_, args_=this`. The two consumers (`TcpListener::ProcessCommand`, `Node::ProcessCommand`) match on `CLOSE_CONNECTION` and destroy the `Connection*`. The `PipedExecutableTaskHandler` already uses `DEFERRED_DELETE` with the identical pattern: `destination_=owner_, args_=this`.

The `type_` field is semantically redundant — the owner knows what it owns based on `destination_` being itself. Removing `CLOSE_CONNECTION` eliminates a redundant enum value and unifies all deferred destruction under one command type.

### How the inlined erasure looks after migration

**`Connection::onEndOfStream()`** (`src/core/io/connection.cc:80-85`):

```cpp
void Connection::onEndOfStream() {
  ::close(fd_);
  mailbox_->Close();
  dispatcher_->SubmitCommand(
      {.type_ = event::Command::DEFERRED_DELETE, .destination_ = owner_, .args_ = this});
}
```

One token changes: `CLOSE_CONNECTION` → `DEFERRED_DELETE`.

**`TcpListener::ProcessCommand()`** (`src/core/io/tcp_listener.cc:68-77`):

```cpp
void TcpListener::ProcessCommand(event::Command cmd) {
  if (cmd.type_ == event::Command::DEFERRED_DELETE) {
    auto* conn = static_cast<Connection*>(cmd.args_);
    auto iter = std::find_if(owned_connections_.begin(), owned_connections_.end(),
                             [conn](const auto& ptr) -> bool { return ptr.get() == conn; });
    if (iter != owned_connections_.end()) {
      owned_connections_.erase(iter);
    }
  }
}
```

Same token swap. The body is unchanged — the `static_cast<Connection*>` and `find_if`+`erase` are the same inlined erasure pattern.

**`Node::ProcessCommand()`** (`src/core/gateway/node.cc:105-110`):

```cpp
void Node::ProcessCommand(event::Command cmd) {
  if (cmd.type_ == event::Command::DEFERRED_DELETE) {
    connection_.reset();
    status_ = Status::kDisconnected;
  }
}
```

Same token swap. The body is unchanged.

### After: `Command::Type` enum

```cpp
enum Type { ACTIVATE_READ, DEFERRED_DELETE } type_{};
```

`CLOSE_CONNECTION` is removed entirely.

## Deferred-delete pattern (documentation)

The deferred-delete pattern is the standard way to destroy objects that may be referenced from the completion stack:

1. An object (e.g. `Connection`, `ChildProcess`) determines it must be destroyed during a completion handler or read/write callback.
2. Instead of deleting itself (unsafe — the completion stack still references `this`), it submits a `DEFERRED_DELETE` command: `{.type_=DEFERRED_DELETE, .destination_=owner_, .args_=this}`.
3. Commands are drained at the top of the next dispatcher `Run()` iteration, outside the completion stack.
4. The owner's `ProcessCommand` erases the object from its owning collection (map, vector), destroying it.

**Invariant:** The owner must hold the object in an owning collection (`unique_ptr` in a `map` or `vector`). The `args_` pointer must be valid until `ProcessCommand` runs.

**Safe re-entry:** If the owner proactively destroys the connection before the queued `DEFERRED_DELETE` arrives (e.g. `Node::Disconnect()` calls `connection_->Close()` which does NOT submit a command), the subsequent `DEFERRED_DELETE` arrives with a stale pointer — but the handler's erasure is a no-op (the collection entry is already gone). This is safe because:
- `Node::Disconnect()` calls `Connection::Close()` which closes the fd and mailbox but does NOT submit a command.
- The `unique_ptr` in the collection is reset directly.
- If a `DEFERRED_DELETE` was already queued before `Disconnect()`, it arrives later and `connection_.reset()` on a null pointer is harmless.

## Risks

None. This is a mechanical rename of an enum value across 2 producers and 2 consumers. Behavioral change is zero.
