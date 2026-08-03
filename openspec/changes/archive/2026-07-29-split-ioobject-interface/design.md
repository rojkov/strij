## Context

Strij's event loop dispatches two kinds of events: I/O completions from io_uring (delivered via `HandleCompletion`) and inter-object messages (delivered via `ProcessCommand`). Currently both are bundled into `event::IOObject`, forcing every participant in the event loop to implement both, even when one is a no-op.

```
Current:

  IOObject
    ├── HandleCompletion()   ← I/O callback (io_uring CQE)
    └── ProcessCommand()     ← message handler (SubmitCommand)

  Classes forced into both:
    Connection      → ProcessCommand = {} (never used)
    LogFrontend     → ProcessCommand = {} (logs "unexpected")
    SignalMonitor   → ProcessCommand = {} (assert only)
```

As the system grows, command-only objects are expected (service coordinators, lifecycle managers, config watchers) that should never implement `HandleCompletion`.

## Goals / Non-Goals

**Goals:**
- Split `IOObject` into two independent interfaces: `Completable` (I/O completion) and `CommandHandler` (messaging)
- Update the `Command` struct to target `CommandHandler*` instead of `IOObject*`
- Update `Dispatcher` signatures to decouple `Prepare*` I/O ops from command delivery
- Each existing class implements only the interface(s) it actually uses
- No behavioural change to the event loop or I/O dispatch

**Non-Goals:**
- No change to the `Command` payload mechanism (`void* args_`) — deferred until more command types emerge
- No separate `CommandBus` — the Dispatcher remains the single delivery mechanism
- No change to the Dispatcher's internal command queue or delivery order

## Decisions

### Decision: Name the interfaces `Completable` and `CommandHandler`

Names describe the role: "something that can complete I/O" and "something that can handle commands". Avoids confusion with the old `IOObject`. No `I` prefix — Strij uses abstract classes as interfaces without marking them in names.

**Alternatives considered:**
- `CompletionHandler` / `CommandReceiver` — more verbose, less grep-friendly
- Keep `IOObject` for one and create `CommandHandler` for the other — would leave the old name for an interface with half the responsibility
- `EventSink` / `MessageSink` — too generic

### Decision: `Completable` handles I/O completions; `CommandHandler` handles messages

```cpp
// completable.hh
namespace strij::event {

class Completable {
public:
  virtual ~Completable() = default;

  Completable(const Completable&) = delete;
  auto operator=(const Completable&) -> Completable& = delete;
  Completable(Completable&&) noexcept = delete;
  auto operator=(Completable&&) noexcept -> Completable& = delete;

  virtual void HandleCompletion(uint8_t tag, int res, uint32_t flags) PURE;
};

} // namespace strij::event
```

```cpp
// command_handler.hh
namespace strij::event {

class CommandHandler {
public:
  virtual ~CommandHandler() = default;

  CommandHandler(const CommandHandler&) = delete;
  auto operator=(const CommandHandler&) -> CommandHandler& = delete;
  CommandHandler(CommandHandler&&) noexcept = delete;
  auto operator=(CommandHandler&&) noexcept -> CommandHandler& = delete;

  virtual void ProcessCommand(Command cmd) PURE;
};

} // namespace strij::event
```

### Decision: Dispatcher `Prepare*` methods take `Completable*`, `SubmitCommand` takes `CommandHandler*`

```cpp
class Dispatcher {
  // I/O operations target Completable
  virtual void PrepareAcceptMultishot(Completable* io, uint8_t tag, int fd) PURE;
  virtual void PrepareRead(Completable* io, uint8_t tag, int fd, ...) PURE;
  virtual void PrepareWrite(Completable* io, uint8_t tag, int fd, ...) PURE;
  virtual void PrepareConnect(Completable* io, uint8_t tag, int fd, ...) PURE;

  // Command delivery targets CommandHandler
  virtual void SubmitCommand(Command cmd) PURE;  // cmd.destination_ is CommandHandler*
};
```

**Rationale:** `io_uring` completions always target a `Completable`. Commands always target a `CommandHandler`. These are orthogonal — an object can implement both, either, or neither.

### Decision: Classes that implement both interfaces use multiple inheritance

```
Before:                   After:
Connection : IOObject     Connection : Completable
Node : IOObject           Node : Completable, CommandHandler
TcpListener : IOObject    TcpListener : Completable, CommandHandler
LogFrontend : IOObject    LogFrontend : Completable
SignalMonitor : IOObject  SignalMonitor : Completable
DispatcherImpl : IOObject DispatcherImpl : Completable, CommandHandler
```

**Rationale:** Multiple inheritance of pure interfaces is safe (no diamond, no state, no ambiguity). Each interface is narrow and single-responsibility. This is the canonical "interface segregation" pattern.

### Decision: Connection's `owner_` field type changes from `IOObject*` to `CommandHandler*`

Connection only uses `owner_` as the destination for `CLOSE_CONNECTION` commands, never for I/O registration. The type narrowing documents this intent.

**Rationale:** Prevents accidental misuse — you can't accidentally register the owner for I/O through Connection.

## Risks / Trade-offs

- [Risk] Multiple inheritance of pure interfaces adds one more vtable per object that implements both → Mitigation: trivial cost (two pointers per object), same vtable cost as the current single `IOObject` with two virtual methods
- [Risk] `DispatcherImpl` casts `io_uring_cqe_get_data` to `Completable*` instead of `IOObject*` → Mitigation: straightforward search-and-replace, compiler catches mismatches
- [Risk] Existing specs reference `IOObject` — all must be updated atomically → Mitigation: change captures all spec revisions as delta specs
