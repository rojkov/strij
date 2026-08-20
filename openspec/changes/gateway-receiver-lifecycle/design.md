## Context

`ResultReceiverStorage` maps `task_id → ResultReceiver` but has no awareness of connection lifecycles. When an HTTP client disconnects before its task completes, or when a node connection drops leaving in-flight tasks orphaned, receivers sit in storage indefinitely. The `OutboundMailbox` already provides `RegisterOnClose` callbacks (used by `ChildProcess` on the nodeagent side and `StateReporter`), but nothing uses them for gateway receiver cleanup.

The two cleanup paths are independent:
1. **HTTP client drops**: the receiver for that one task becomes useless — the connection to write results back is gone.
2. **Node connection drops**: all receivers for tasks routed to that node become orphaned — results will never arrive.

Both must be handled. Both use the same underlying mechanism (`OutboundMailbox::RegisterOnClose`), but at different granularities.

## Goals / Non-Goals

**Goals:**
- Clean up `ResultReceiverStorage` on HTTP client connection drop (per-receiver).
- Clean up `ResultReceiverStorage` on node connection drop (per-node batch).
- Deliver error responses to still-connected HTTP clients when a node drops.
- Keep `ExactStateTracker` purely for resource accounting — no new responsibilities.
- Keep the cleanup self-contained in `ResultReceiverStorage` so it survives the future D10 cached/distributed state model swap.

**Non-Goals:**
- Node reconnection logic (Phase 3 roadmap item — separate change).
- Task timeouts (Phase 2 roadmap item — separate change).
- Thread safety for `ResultReceiverStorage` (all access is on the event-loop thread).
- Cleaning up `per_node_` entries in `ExactStateTracker` after node removal (left at zero; corrected by next heartbeat if the node reappears).

## Decisions

### D1: Extend `ResultReceiverStorage` with `task_id → node_id` mapping

**Decision**: `ResultReceiverStorage` owns a `node_of_task_` map populated by `put(task_id, receiver, node_id)` and cleared by `erase()`. A new `NotifyNodeDisconnected(node_id)` method iterates this map, finds all receivers for the node, delivers errors, erases receivers, and calls `RecordCompletion` on the state tracker.

**Alternatives considered:**
- *Put `TasksForNode()` on `ExactStateTracker`*: rejected because the tracker is a v1 mechanism explicitly slated for replacement by the D10 cached/distributed model. Coupling receiver cleanup to it would make the tracker harder to replace.
- *Put cleanup in `NodeDirectory`*: rejected because `NodeDirectory` doesn't own receivers and would need to reach into both storage and tracker, creating a god-object.

**Rationale**: `ResultReceiverStorage` already owns the receiver map. Adding a node index keeps cleanup self-contained and decoupled from the state model. The `node_of_task_` map is small (bounded by in-flight concurrency) and has the same lifetime as the receiver entries.

### D2: HTTP client cleanup via mailbox close callback in `GatewayHttpHandler`

**Decision**: After `storage_.put(task_id, receiver)`, register a close callback on the HTTP connection's mailbox:

```cpp
conn.Mailbox()->RegisterOnClose(
    [&storage = storage_, task_id, &tracker = state_tracker_]() {
      storage.erase(task_id);
      if (tracker != nullptr) {
        tracker->RecordCompletion(task_id);
      }
    });
```

**Rationale**: The callback fires synchronously within `onEndOfStream()` while the `Connection` object is still alive. `unordered_map::erase` for a missing key is a no-op, so the normal final-result path (which also calls `erase`) doesn't conflict.

### D3: Node cleanup via mailbox close callback in `Node`

**Decision**: `Node` gains `storage_` and `state_tracker_` references (passed through `NodeDirectory`). When the connection is created in `HandleCompletion(kConnect)`, register a close callback:

```cpp
close_token_ = connection_->Mailbox()->RegisterOnClose([this]() {
  storage_.NotifyNodeDisconnected(node_id_);
});
```

**Alternatives considered:**
- *Register in `GatewayTlvHandler`*: rejected because the handler is moved into the parser and destroyed with the connection — the callback's captures would dangle.
- *Register in `NodeDirectory::RemoveNode()`*: rejected because `RemoveNode` is an explicit removal (discovery-driven), while the close callback handles unexpected drops (network failure, nodeagent crash). Both paths must clean up.

**Rationale**: `Node` already owns the connection lifecycle. Adding two references is minimal, and the close callback fires in both the `onEndOfStream` (unexpected drop) and `Connection::Close()` (explicit removal) paths.

### D4: Error delivery to HTTP clients on node drop

**Decision**: `NotifyNodeDisconnected` calls `receiver->DeliverError("node disconnected")` for each orphaned receiver. The `HttpResponseFramer` handles this gracefully: if idle, it sends a 503; if already streaming, `ErrorResponse()` returns an empty vector (no-op) and the client sees the connection close.

**Rationale**: Attempting error delivery is cheap (a no-op when the HTTP connection is already dead) and gives still-connected clients a meaningful 503 response instead of a silent connection close.

### D5: Sequence diagrams for the two cleanup paths

**HTTP client drop:**
```
  HTTP client          Gateway                    Node Agent
       │                    │                          │
       │── POST /tasks ────▶│                          │
       │                    │ storage.put(id, recv)    │
       │                    │ mailbox.RegisterOnClose()│
       │                    │── TLV TaskSubmission ───▶│
       │                    │                          │
       ╳  DROPS             │                          │
       │              ┌─────┴──────┐                   │
       │              │ onEndOfStream                  │
       │              │ mailbox.Close()                │
       │              │   callback:                    │
       │              │     storage.erase(id)          │
       │              │     tracker.RecordCompletion   │
       │              └────────────┘                   │
       │                    │                          │
       │                    │◀── kResult (late) ───────│
       │                    │   storage.get(id)→nullptr│
       │                    │   → warning, dropped     │
```

**Node connection drop:**
```
  Node Agent           Network              Gateway
       │                  │                     │
       │◀═══ RST/timeout ═╳                     │
       │                  │              ┌──────┴──────┐
       │                  │              │ onEndOfStream│
       │                  │              │ mailbox.Close│
       │                  │              │   callback:  │
       │                  │              │   NotifyNode │
       │                  │              │   Disconnected│
       │                  │              │     find tasks│
       │                  │              │     recv→     │
       │                  │              │       DeliverError│
       │                  │              │     storage.erase│
       │                  │              │     tracker.Record│
       │                  │              └─────────────┘
```

## Risks / Trade-offs

- **[Double cleanup idempotency]** If both the HTTP and node callbacks fire for the same task_id (both connections drop), `storage.erase()` is called twice. This is safe: `unordered_map::erase` for a missing key is a no-op. The `tracker.RecordCompletion` call is also idempotent (no-op for unknown task_id). → No mitigation needed; inherent in the design.

- **[Stale `per_node_` in ExactStateTracker]** After cleaning up all tasks for a disconnected node, the `per_node_[node_id]` entry stays at zero in-flight. If the node reappears, the next `kNodeState` heartbeat corrects it via `ApplyStateSnapshot`. If the node never returns, the entry is harmless memory. → Acceptable; the entry is small and bounded by the number of distinct nodes ever seen.

- **[Close callback captures reference to `storage_` on stack in `gateway.cc`]** The `ResultReceiverStorage` is a local variable in `main()`. The close callbacks capture `&storage_`. During shutdown, `dispatcher->Run()` returns, then the `TcpListener` is destroyed (destroying connections, firing close callbacks), then `storage` is destroyed. The ordering is safe because `TcpListener` destruction happens before `storage` destruction. → No mitigation needed; destruction order is correct.

- **[`NotifyNodeDisconnected` iterates `node_of_task_`]** The iteration is O(N) where N is in-flight tasks for the node. This is bounded by the node's concurrency limits (typically small). The method runs on the event-loop thread during connection teardown — a one-time cost per disconnect. → Acceptable; no worse than existing `RecordCompletion` lookups.
