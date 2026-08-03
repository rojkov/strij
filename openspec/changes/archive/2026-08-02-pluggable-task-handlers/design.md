## Context

`NodeagentTlvHandler::HandleFrame` hardcodes task processing: parse a `Task` from a `kTaskSubmission` frame and echo a `TaskResult` back (`src/core/nodeagent/nodeagent_tlv_handler.cc`). The extension infrastructure to make this pluggable already exists: `Registry<FactoryInterface>` with static `REGISTER_FACTORY` macros, `ExtensionConfig {name, typed_config(Any)}` config, and the `NodeDiscovery`/`NodeDiscoveryFactory` pattern (`src/extensions/node_discovery/`). The gateway consumes node discovery through this mechanism in `src/exe/gateway/gateway.cc`; the nodeagent consumes none of it yet.

This change applies the same pattern to nodeagent task processing, with a `TaskHandlerManager` as the shared owner of instantiated handlers.

## Goals / Non-Goals

**Goals:**
- Remove hardcoded echo from `NodeagentTlvHandler`; route tasks to a `TaskHandler` by `task.type()`.
- Establish the `TaskHandler`/`TaskHandlerFactory` extension category following the `NodeDiscovery` pattern.
- Support future asynchronous handlers (process spawning, streaming results) without an interface-breaking change later.
- Preserve current behavior: with `echo` configured, the round-trip is byte-identical to today.

**Non-Goals:**
- Runtime reconfiguration of the handler set (API seam only).
- The actual process-spawning/streaming handlers, and `is_final`-aware delivery on the gateway side (field added now, consumed later).
- Any changes to the gateway's result handling.

## Decisions

### D1: `TaskHandler` is push-based via `ResultSender`, not `Connection&` and not "return TaskResult"

```cpp
// src/extensions/task_handlers/task_handlers.hh
class ResultSender {
public:
  virtual ~ResultSender() = default;
  virtual void Send(strij::task::TaskResult result) PURE;
};

class TaskHandler {
public:
  virtual ~TaskHandler() = default;
  virtual void HandleTask(const strij::task::Task& task, ResultSender& sender) PURE;
};
```

- The handler never touches a `Connection` — it delivers results through the sender. A sync handler (echo) calls `sender.Send(...)` and returns; an async handler holds the sender and sends later.
- **Why not `Connection&`?** Async handlers would have to stash a raw `Connection&` for later use; the gateway may disconnect in between → dangling reference. The sender is the per-frame, connection-bound indirection that owns that lifetime question.
- **Why not "return `TaskResult`"?** An async/streaming handler has nothing to return; it must push. A return-based signature would need to be replaced the moment streaming arrives. The sender is the one shape that covers both.
- Symmetric with the gateway's existing `ResultReceiver` / `HttpResultReceiver` (`result_receiver_storage.hh`).

Concrete sender (nodeagent side, binds the connection):

```cpp
// src/core/nodeagent/result_sender.hh
class ConnectionResultSender final : public ResultSender {
public:
  explicit ConnectionResultSender(strij::io::Connection& conn);
  void Send(strij::task::TaskResult result) override;  // serialize + SerializeTlvFrame(kResult) + conn.Write
private:
  strij::io::Connection& conn_;
};
```

This is the current echo body moved into a type.

**Is the `Connection&` in the sender safe?** For this change, yes. `Connection` is `final` and owned as a `unique_ptr` by `TcpListener::owned_connections_` (`connection.hh`, `tcp_listener.hh`). `HandleFrame` runs synchronously inside the connection's read completion on the event-loop thread, so the connection is provably alive for the whole call; the sender is constructed and consumed within that call for sync handlers. The reference is only problematic once a handler holds the sender past `HandleFrame` (the async path): the connection may then be torn down — also on the event-loop thread — and the `Connection&` dangles. That is deferred, not silent: before wiring the first async handler we must pick one of (a) change `Connection` ownership to `shared_ptr` and have the sender hold a weak ref with a liveness check, (b) expose a connection state flag and guarantee pending senders are drained/cancelled at teardown, or (c) deliver results through a connection-owned mailbox that is dropped on close. Until then, only synchronous delivery is wired.

### D2: `TaskHandlerManager` is shared, keyed by factory name

```cpp
// src/core/nodeagent/task_handler_manager.hh
class TaskHandlerManager {
public:
  auto GetHandler(const std::string& type) const -> TaskHandler*;
  void AddHandler(std::string type, std::unique_ptr<TaskHandler> handler);   // runtime-update seam
  void RemoveHandler(const std::string& type);                               // runtime-update seam
  bool empty() const;
private:
  std::unordered_map<std::string, std::unique_ptr<TaskHandler>> handlers_;
};

// Free builder (also in task_handler_manager module)
auto BuildTaskHandlerManager(
    const ::google::protobuf::RepeatedPtrField<strij::config::ExtensionConfig>& configs,
    FactoryContext& context) -> absl::StatusOr<std::shared_ptr<TaskHandlerManager>>;
```

- **Shared because handlers hold shared state** (future: one process pool serving all connections). `NodeagentTlvHandler` is created per accepted connection; it holds a `std::shared_ptr<TaskHandlerManager>` injected via constructor.
- **Keyed by factory `Name()`**, which equals the task type (registry lookup and `GetHandler` agree on the same strings).
- **Builder keeps the manager a dumb container** and owns the Registry lookup + `Any` unpack + `Create` wiring; mutation methods are the future reconfiguration seam.
- Handlers are instantiated once at startup. Because all frames and completions run on the single event-loop thread, per-task state inside a shared handler needs no locking (a future async handler keys state by `task_id`).

### D3: Config and loading semantics

```proto
// api/core/config/nodeagent.proto — task_handlers = 6 (3/4/5 stay reserved)
message NodeAgentConfig {
  TlvListener tlv_listener = 1;
  Logging logging = 2;
  // RESERVED for future (v2+): connection_timeout = 3, heartbeat_interval = 4, tls = 5
  repeated ExtensionConfig task_handlers = 6;
}
```

In `nodeagent.cc`, after config load and before `--validate_only` short-circuit (mirroring the gateway's node_discovery check at `gateway.cc:52-62`):

1. Build the manager from `config.task_handlers()`. Empty list → `LOG_WARNING` ("no task handlers configured; all tasks will be dropped") and continue.
2. For each entry: `Registry<TaskHandlerFactory>::instance().GetFactory(name)` → `nullptr` ⇒ startup fails with an error naming the handler. Otherwise unpack `Any` via `factory->CreateEmptyConfigProto()` and `Create(config_msg, factory_context)`.
3. Pass the manager as `shared_ptr` into the TcpListener connection factory, which constructs `NodeagentTlvHandler(manager)` per connection.

Example YAML (also reflected in `config/examples/nodeagent.yaml`):

```yaml
task_handlers:
  - name: "echo"
    typed_config:
      "@type": "type.googleapis.com/strij.extensions.task_handlers.echo.EchoTaskHandlerConfig"
```

`EchoTaskHandlerConfig` is an empty proto (any `Any` round-trip needs a concrete type).

### D4: Extension layout mirrors node_discovery

```
src/extensions/task_handlers/
├── task_handlers.hh                   TaskHandler + ResultSender + TaskHandlerFactory
├── BUILD.bazel                        task_handlers_interface
└── echo/
    ├── echo_task_handler.hh/.cc       EchoTaskHandler + EchoTaskHandlerFactory
    ├── echo_task_handler.proto        EchoTaskHandlerConfig (empty)
    └── BUILD.bazel                    echo_task_handler_lib (alwayslink = True)

api/extensions/task_handlers/echo/echo_task_handler.proto   → extensions/task_handlers/echo/...
src/core/nodeagent/
├── task_handler_manager.hh/.cc        manager + BuildTaskHandlerManager
├── result_sender.hh/.cc               ConnectionResultSender
└── nodeagent_tlv_handler.hh/.cc       + shared_ptr<TaskHandlerManager> ctor, routing
```

`EchoTaskHandler::HandleTask` builds `TaskResult{id=task.id(), body=task.body(), is_final=true}` and calls `sender.Send(...)` — today's echo, verbatim.

### D5: `TaskResult.is_final` as `optional` (absence ⇒ final, encoded in code)

```proto
// api/core/task/task.proto
message TaskResult {
  string id = 1;
  bytes body = 2;
  optional bool is_final = 3;  // absence means final
}
```

- proto3 forbids explicit `[default = true]` on `optional` fields, and a proto3 `optional bool` reads back as `false` when unset. "Absence ⇒ final" is therefore encoded in consumer code: treat a result as final when `!result.has_is_final() || result.is_final()`. Producers of single-shot results may leave the field unset; `EchoTaskHandler` sets it to `true` explicitly.
- This keeps results from old nodeagents looking final to a future `is_final`-aware gateway (they omit the field → absent → final).
- Today's gateway ignores the field, so this is additive until the streaming change consumes it.

### D6: FactoryContext consolidation

`GatewayFactoryContext` → `FactoryContextImpl` (`src/core/extensions/factory_context.{hh,cc}`), a general `{Dispatcher&, Logger&}` provider over a `DispatcherSharedPtr`, usable by both gateway and nodeagent. Add `MockFactoryContext` to the test mocks for unit tests that don't have a real dispatcher.

### D7: Echo flow (sequence)

```
gateway conn ─► NodeagentTlvHandler::HandleFrame(frame, conn)
   │  type_id == kTaskSubmission
   │  parse Task (malformed → LOG_WARNING, drop)
   ▼
   handler = manager_->GetHandler(task.type())
   │  null → LOG_WARNING, drop
   ▼
   ConnectionResultSender sender(conn)
   handler->HandleTask(task, sender)
      └─ EchoTaskHandler: Send(TaskResult{id, body, is_final=true})
           └─ serialize → SerializeTlvFrame(kResult) → conn.Write
```

Future async shape (not built now): the handler starts work, completes later on the event-loop thread, and calls the same `sender.Send(...)` zero-to-many times with `is_final=false/true`.

## Risks / Trade-offs

- **Shared handlers must be stateful only in task-id-keyed, event-loop-local state** → [Future async handlers key in-flight state by `task_id`; document the constraint in `task_handlers.hh`.]
- **`Connection&` inside `ConnectionResultSender` across async boundaries** → Safe for the synchronous delivery wired in this change (sender lives within `HandleFrame`, on the event-loop thread, connection owned alive by `TcpListener`). The constraint is documented on both `ResultSender` and `ConnectionResultSender`: senders must not outlive `HandleTask()` — async delivery is blocked by contract until then. When the first async handler lands, resolve via weak/shared ownership or a connection-owned result mailbox (see D1). Noted as a follow-up.
- **Global `Registry<TaskHandlerFactory>` singleton and tests** → Tests must not collide on names; use the direct `RegisterFactory(name, new MockTaskHandlerFactory)` API with unique names, or inject handlers into the manager directly.
- **Existing nodeagent configs without `task_handlers` start with everything dropped** → Intentional (config-first); a loud warning fires and `config/examples/nodeagent.yaml` is updated. Rollback is a revert.
- **`is_final` unused by the gateway until streaming lands** → Additive field, no behavior change; gateway consumption is a separate future change.

## Migration Plan

1. Land protocol/config/manager/interface first (no behavior change to tests yet possible: echo still echoes).
2. Land `EchoTaskHandler` + `nodeagent.cc` wiring + example config; existing `nodeagent_tlv_handler_test` semantics preserved via a manager containing the echo handler.
3. Rollback: revert the wiring commit; `NodeagentTlvHandler` reverts to hardcoded echo.

## Open Questions

- Should `--validate_only` also validate that all `task_handlers` names are registered? (Precedent: gateway validates node_discovery presence before `validate_only` returns — leaning yes, covered by the same startup check.)
- Test-mock layout: a single `test/mocks/extensions/` lib for `MockFactoryContext`, `MockTaskHandler`, `MockTaskHandlerFactory`, `MockResultSender` — acceptable to co-locate in one mocks target.
