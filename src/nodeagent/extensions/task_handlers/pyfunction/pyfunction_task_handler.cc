#include "nodeagent/extensions/task_handlers/pyfunction/pyfunction_task_handler.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "common/core/logging/log.hh"
#include "common/core/utils/task_id.hh"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/pyfunction/worker_process.hh"
#include "strij/event/command.hh"
#include "strij/extensions/extension_registry.hh"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij::nodeagent::task_handlers::pyfunction {

struct PyFunctionTaskHandler::Slot {
  enum class State : std::uint8_t { kStarting = 0, kIdle = 1, kRunning = 2, kDead = 3 };

  std::unique_ptr<WorkerProcess> worker_owner_;
  WorkerProcess* worker{nullptr};
  State state_{State::kDead};
  std::uint64_t generation_{0};

  absl::Time startup_deadline_{absl::InfinitePast()};
  absl::Time respawn_deadline_{absl::InfinitePast()};
  absl::Time task_deadline_{absl::InfinitePast()};

  // TODO: why so many booleans? Perhaps we'd better resort to FSM?
  bool task_active_{false};
  bool drop_result_{false};
  bool close_registered_{false};
  std::size_t close_token_{0};

  std::string task_id_;
  std::string function_;
  std::string body_;
  InvocationMode mode_{INVOCATION_MODE_UNSPECIFIED};
  std::map<std::string, std::string> parameters_;
  ResultSenderPtr sender_;
  std::unordered_map<std::string, absl::Time> remote_calls_;
};

namespace {

void sendRemoteResult(PyFunctionTaskHandler::Slot& slot, const std::string& call_id,
                      const std::string& body, task::TaskStatus status, const std::string& reason,
                      bool is_final) {
  if (slot.worker == nullptr) {
    return;
  }

  WorkerFrame frame;
  auto* result = frame.mutable_remote_result();
  result->set_call_id(call_id);
  result->set_body(body);
  result->set_is_final(is_final);
  result->set_status(static_cast<std::uint32_t>(status));
  result->set_reason(reason);
  slot.worker->Send(frame);
}

class RemoteCallReceiver final : public gateway::ResultReceiver {
public:
  RemoteCallReceiver(std::weak_ptr<PyFunctionTaskHandler::Slot> slot, std::uint64_t generation,
                     std::string call_id)
      : slot_{std::move(slot)}, generation_{generation}, call_id_{std::move(call_id)} {}

  void Deliver(std::span<const std::byte> value, bool is_final) override {
    auto slot = slot_.lock();
    if (slot == nullptr || slot->generation_ != generation_) {
      return;
    }

    auto call = slot->remote_calls_.find(call_id_);
    if (call == slot->remote_calls_.end()) {
      return;
    }

    if (is_final) {
      slot->remote_calls_.erase(call);
    }

    // An empty child payload leaves `value.data()` null, which must not reach
    // the std::string(const char*, size_type) constructor.
    std::string body;
    if (!value.empty()) {
      body.assign(std::bit_cast<const char*>(value.data()), value.size());
    }

    sendRemoteResult(*slot, call_id_, body, task::TASK_STATUS_OK, {}, is_final);
  }

  void DeliverError(std::string_view reason, task::TaskStatus status) override {
    auto slot = slot_.lock();
    if (slot == nullptr || slot->generation_ != generation_) {
      return;
    }

    auto call = slot->remote_calls_.find(call_id_);
    if (call == slot->remote_calls_.end()) {
      return;
    }

    slot->remote_calls_.erase(call);
    sendRemoteResult(*slot, call_id_, {}, status, std::string(reason), true);
  }

private:
  std::weak_ptr<PyFunctionTaskHandler::Slot> slot_;
  std::uint64_t generation_;
  std::string call_id_;
};

auto taskStatusIsOk(task::TaskStatus status) -> bool { return status == task::TASK_STATUS_OK; }

} // namespace

PyFunctionTaskHandler::PyFunctionTaskHandler(event::Dispatcher& dispatcher,
                                             ChildTaskSubmitter& submitter, PyFunctionConfig config)
    : dispatcher_{&dispatcher}, submitter_{&submitter}, config_{std::move(config)},
      timer_{dispatcher, [this]() { expireDeadlines(); }} {
  slots_.reserve(static_cast<std::size_t>(config_.concurrency_));

  for (std::uint64_t index = 0; index < config_.concurrency_; ++index) {
    auto slot = std::make_shared<Slot>();
    slots_.push_back(slot);
    spawnSlot(slot);
  }

  rescheduleDeadlines();
}

PyFunctionTaskHandler::~PyFunctionTaskHandler() = default;

auto PyFunctionTaskHandler::makeSpawnConfig() -> WorkerSpawnConfig {
  WorkerSpawnConfig spawn;
  spawn.max_frame_bytes_ = config_.max_frame_bytes_;

  if (!config_.worker_executable_.empty()) {
    spawn.program_ = config_.worker_executable_;
  } else {
    spawn.program_ = config_.python_executable_.empty() ? "python3" : config_.python_executable_;
    spawn.arguments_ = {"-m", config_.worker_module_};
  }

  spawn.environment_.emplace_back("PYTHONUNBUFFERED=1");
  if (!config_.python_paths_.empty()) {
    std::string joined;
    for (const auto& path : config_.python_paths_) {
      if (!joined.empty()) {
        joined.push_back(':');
      }

      joined += path;
    }

    // Preserve the inherited PYTHONPATH: the configured directories are
    // prepended, not substituted.
    if (const char* inherited = std::getenv("PYTHONPATH");
        inherited != nullptr && *inherited != '\0') {
      if (!joined.empty()) {
        joined.push_back(':');
      }

      joined += inherited;
    }

    spawn.environment_.push_back("PYTHONPATH=" + joined);
  }

  return spawn;
}

void PyFunctionTaskHandler::spawnSlot(const std::shared_ptr<Slot>& slot) {
  ++slot->generation_;
  slot->drop_result_ = false;
  slot->respawn_deadline_ = absl::InfinitePast();

  const std::uint64_t generation = slot->generation_;
  std::weak_ptr<Slot> weak = slot;

  WorkerCallbacks callbacks;
  callbacks.on_ready_ = [this, weak, generation](std::uint32_t version) {
    if (auto locked = weak.lock(); locked != nullptr && locked->generation_ == generation) {
      handleReady(locked, version);
    }
  };
  callbacks.on_frame_ = [this, weak, generation](const WorkerFrame& frame) {
    if (auto locked = weak.lock(); locked != nullptr && locked->generation_ == generation) {
      handleFrame(locked, frame);
    }
  };
  callbacks.on_exit_ = [this, weak, generation]() {
    if (auto locked = weak.lock(); locked != nullptr && locked->generation_ == generation) {
      handleWorkerExit(locked, generation);
    }
  };

  auto worker =
      std::make_unique<WorkerProcess>(*dispatcher_, makeSpawnConfig(), std::move(callbacks));
  if (!worker->Ok()) {
    LOG_WARNING("pyfunction: worker spawn failed; retrying slot");
    slot->worker = nullptr;
    slot->state_ = Slot::State::kDead;
    slot->respawn_deadline_ = absl::Now() + absl::Seconds(1);
    return;
  }

  slot->worker_owner_ = std::move(worker);
  slot->worker = slot->worker_owner_.get();
  slot->state_ = Slot::State::kStarting;
  slot->startup_deadline_ = absl::Now() + config_.startup_timeout_;
  slot->worker->Start();
}

void PyFunctionTaskHandler::retireWorker(const std::shared_ptr<Slot>& slot) {
  slot->worker = nullptr;
  if (!slot->worker_owner_) {
    return;
  }

  auto* raw = slot->worker_owner_.get();
  retired_workers_.push_back(std::move(slot->worker_owner_));
  dispatcher_->SubmitCommand(
      {.type_ = event::Command::DEFERRED_DELETE, .destination_ = this, .args_ = raw});
}

void PyFunctionTaskHandler::scheduleRespawn(const std::shared_ptr<Slot>& slot) {
  slot->state_ = Slot::State::kDead;
  // A worker that exits before completing the handshake (for example because
  // its Python/protobuf environment is broken) would otherwise respawn in a
  // tight loop. Back off before retrying; a worker that became ready and then
  // crashed keeps the immediate respawn path.
  slot->respawn_deadline_ = absl::Now() + absl::Seconds(1);
}

void PyFunctionTaskHandler::HandleTask(const task::Task& task, ResultSenderPtr sender) {
  std::string function;
  InvocationMode mode = INVOCATION_MODE_PICKLE;
  auto function_iter = task.parameters().find("function");
  if (function_iter != task.parameters().end()) {
    mode = INVOCATION_MODE_JSON;
    function = function_iter->second;
  }

  std::shared_ptr<Slot> chosen;
  for (const auto& slot : slots_) {
    if (slot->state_ == Slot::State::kIdle) {
      chosen = slot;
      break;
    }
  }

  if (chosen == nullptr) {
    for (const auto& slot : slots_) {
      if (slot->state_ == Slot::State::kStarting && !slot->task_active_) {
        chosen = slot;
        break;
      }
    }
  }

  if (chosen == nullptr) {
    deliverUnavailable(task, *sender);
    return;
  }

  std::map<std::string, std::string> parameters(task.parameters().begin(), task.parameters().end());
  assignTask(chosen, task.id(), task.body(), mode, std::move(function), std::move(parameters),
             std::move(sender));
}

void PyFunctionTaskHandler::assignTask(const std::shared_ptr<Slot>& slot, std::string task_id,
                                       std::string body, InvocationMode mode, std::string function,
                                       std::map<std::string, std::string> parameters,
                                       ResultSenderPtr sender) {
  slot->task_active_ = true;
  slot->drop_result_ = false;
  slot->task_id_ = std::move(task_id);
  slot->body_ = std::move(body);
  slot->mode_ = mode;
  slot->function_ = std::move(function);
  slot->parameters_ = std::move(parameters);
  slot->sender_ = std::move(sender);

  std::weak_ptr<Slot> weak = slot;
  slot->close_token_ = slot->sender_->RegisterOnClose([this, weak]() {
    if (auto locked = weak.lock()) {
      handleDisconnect(locked);
    }
  });
  slot->close_registered_ = true;

  if (slot->state_ == Slot::State::kIdle) {
    sendRun(*slot);
    slot->state_ = Slot::State::kRunning;
    slot->task_deadline_ = config_.task_timeout_ > absl::ZeroDuration()
                               ? absl::Now() + config_.task_timeout_
                               : absl::InfinitePast();
  }
  // A starting slot holds the task until Ready; HandleTask never queues.
  rescheduleDeadlines();
}

void PyFunctionTaskHandler::sendRun(Slot& slot) {
  if (slot.worker == nullptr) {
    return;
  }

  WorkerFrame frame;
  auto* run = frame.mutable_run();
  run->set_task_id(slot.task_id_);
  run->set_mode(slot.mode_);
  run->set_function(slot.function_);
  run->set_body(slot.body_);

  for (const auto& [key, value] : slot.parameters_) {
    (*run->mutable_parameters())[key] = value;
  }

  slot.worker->Send(frame);
}

void PyFunctionTaskHandler::handleReady(const std::shared_ptr<Slot>& slot, std::uint32_t version) {
  if (version != kProtocolVersion) {
    LOG_WARNING("pyfunction: worker {} reported protocol version {} (expected {})",
                slot->worker != nullptr ? slot->worker->Pid() : -1, version, kProtocolVersion);
    if (slot->worker != nullptr) {
      slot->worker->Kill();
    }

    slot->state_ = Slot::State::kDead;

    return;
  }

  if (slot->state_ == Slot::State::kDead) {
    return;
  }

  LOG_INFO("pyfunction: worker {} ready", slot->worker != nullptr ? slot->worker->Pid() : -1);
  slot->startup_deadline_ = absl::InfinitePast();
  slot->state_ = Slot::State::kIdle;

  if (slot->task_active_) {
    sendRun(*slot);
    slot->state_ = Slot::State::kRunning;
    slot->task_deadline_ = config_.task_timeout_ > absl::ZeroDuration()
                               ? absl::Now() + config_.task_timeout_
                               : absl::InfinitePast();
  }

  rescheduleDeadlines();
}

void PyFunctionTaskHandler::handleFrame(const std::shared_ptr<Slot>& slot,
                                        const WorkerFrame& frame) {
  if (frame.has_run_result()) {
    handleRunResult(slot, frame.run_result());
  } else if (frame.has_remote_call()) {
    handleRemoteCall(slot, frame.remote_call());
  }
}

void PyFunctionTaskHandler::handleRunResult(const std::shared_ptr<Slot>& slot,
                                            const RunResult& result) {
  if (!slot->task_active_ || result.task_id() != slot->task_id_) {
    return;
  }

  const auto status = static_cast<task::TaskStatus>(result.status());
  if (taskStatusIsOk(status) && !result.is_final()) {
    task::TaskResult chunk;
    chunk.set_id(slot->task_id_);
    chunk.set_body(result.body());
    chunk.set_is_final(false);
    chunk.set_status(status);
    slot->sender_->Send(std::move(chunk));
    return;
  }

  finishTask(slot, result.body(), status, result.reason(), true);
}

void PyFunctionTaskHandler::handleRemoteCall(const std::shared_ptr<Slot>& slot,
                                             const RemoteCall& call) {
  if (!slot->task_active_ || call.call_id().empty()) {
    return;
  }

  task::Task child;
  child.set_id(utils::GenerateTaskId());
  child.set_type("pyfunction");
  child.set_body(call.invocation());

  for (const auto& [key, value] : call.parameters()) {
    (*child.mutable_parameters())[key] = value;
  }

  for (const auto& [pool, amount] : call.requirements()) {
    (*child.mutable_requirements()->mutable_resources())[pool] = amount;
  }

  slot->remote_calls_[call.call_id()] = config_.remote_call_timeout_ > absl::ZeroDuration()
                                            ? absl::Now() + config_.remote_call_timeout_
                                            : absl::InfinitePast();

  submitter_->Submit(std::move(child),
                     std::make_unique<RemoteCallReceiver>(slot, slot->generation_, call.call_id()));
  rescheduleDeadlines();
}

void PyFunctionTaskHandler::finishTask(const std::shared_ptr<Slot>& slot, std::string body,
                                       task::TaskStatus status, std::string reason,
                                       bool worker_healthy) {
  if (!slot->task_active_) {
    return;
  }

  if (slot->close_registered_) {
    slot->sender_->UnregisterOnClose(slot->close_token_);
    slot->close_registered_ = false;
  }

  task::TaskResult out;
  out.set_id(slot->task_id_);
  if (taskStatusIsOk(status)) {
    out.set_body(std::move(body));
  } else if (!reason.empty()) {
    out.set_body(std::move(reason));
  } else {
    out.set_body(std::move(body));
  }

  out.set_status(status);
  out.set_is_final(true);
  slot->sender_->Send(std::move(out));
  slot->sender_.reset();

  slot->task_active_ = false;
  slot->drop_result_ = false;
  slot->task_deadline_ = absl::InfinitePast();
  slot->task_id_.clear();

  // Pending nested calls belong to the task that just finished. Dropping them
  // now keeps their deadlines from staying armed and prevents a late child
  // outcome from being written to this worker while it runs another task.
  slot->remote_calls_.clear();

  if (worker_healthy && slot->worker != nullptr && slot->state_ != Slot::State::kDead) {
    slot->state_ = Slot::State::kIdle;
  } else if (!worker_healthy) {
    slot->state_ = Slot::State::kDead;
  }

  rescheduleDeadlines();
}

void PyFunctionTaskHandler::deliverUnavailable(const task::Task& task, ResultSender& sender) {
  task::TaskResult out;
  out.set_id(task.id());
  out.set_body("no pyfunction worker is available");
  out.set_status(task::TASK_STATUS_UNAVAILABLE);
  out.set_is_final(true);
  sender.Send(std::move(out));
}

void PyFunctionTaskHandler::handleWorkerExit(const std::shared_ptr<Slot>& slot,
                                             std::uint64_t /*generation*/) {
  const bool exited_before_ready = slot->state_ == Slot::State::kStarting;
  retireWorker(slot);

  if (slot->task_active_) {
    if (slot->drop_result_) {
      if (slot->close_registered_) {
        slot->sender_->UnregisterOnClose(slot->close_token_);
        slot->close_registered_ = false;
      }
      slot->sender_.reset();
      slot->task_active_ = false;
      slot->task_deadline_ = absl::InfinitePast();
      slot->task_id_.clear();
    } else {
      finishTask(slot, {}, task::TASK_STATUS_INTERNAL, "the pyfunction worker exited", false);
    }
  }

  // Outcomes of children still in flight are dropped: their receivers see a
  // stale generation or a missing call id and do not deliver to the new worker.
  slot->remote_calls_.clear();
  if (exited_before_ready) {
    scheduleRespawn(slot);
  } else {
    spawnSlot(slot);
  }

  rescheduleDeadlines();
}

void PyFunctionTaskHandler::handleDisconnect(const std::shared_ptr<Slot>& slot) {
  if (!slot->task_active_) {
    return;
  }

  slot->drop_result_ = true;
  slot->close_registered_ = false;
  slot->task_deadline_ = absl::InfinitePast();

  if (slot->worker != nullptr) {
    slot->worker->Kill();
    slot->state_ = Slot::State::kDead;
  } else {
    slot->sender_.reset();
    slot->task_active_ = false;
    slot->task_id_.clear();
  }

  rescheduleDeadlines();
}

void PyFunctionTaskHandler::expireDeadlines() {
  const absl::Time now = absl::Now();

  for (const auto& slot : slots_) {
    if (slot->state_ == Slot::State::kStarting && slot->startup_deadline_ != absl::InfinitePast() &&
        now >= slot->startup_deadline_) {
      LOG_WARNING("pyfunction: worker startup timed out");
      slot->startup_deadline_ = absl::InfinitePast();
      if (slot->task_active_) {
        finishTask(slot, {}, task::TASK_STATUS_DEADLINE_EXCEEDED,
                   "the pyfunction worker did not become ready in time", false);
      }

      slot->state_ = Slot::State::kDead;

      if (slot->worker != nullptr) {
        slot->worker->Kill();
      }
    }

    if (slot->state_ == Slot::State::kRunning && slot->task_deadline_ != absl::InfinitePast() &&
        now >= slot->task_deadline_) {
      LOG_WARNING("pyfunction: task '{}' timed out", slot->task_id_);
      slot->task_deadline_ = absl::InfinitePast();

      if (slot->task_active_) {
        finishTask(slot, {}, task::TASK_STATUS_DEADLINE_EXCEEDED, "the pyfunction task timed out",
                   false);
      }

      slot->state_ = Slot::State::kDead;

      if (slot->worker != nullptr) {
        slot->worker->Kill();
      }
    }

    if (slot->state_ == Slot::State::kDead && slot->respawn_deadline_ != absl::InfinitePast() &&
        now >= slot->respawn_deadline_) {
      spawnSlot(slot);
    }

    std::vector<std::string> expired_calls;
    for (const auto& [call_id, deadline] : slot->remote_calls_) {
      if (deadline != absl::InfinitePast() && now >= deadline) {
        expired_calls.push_back(call_id);
      }
    }

    for (const auto& call_id : expired_calls) {
      slot->remote_calls_.erase(call_id);
      sendRemoteResult(*slot, call_id, {}, task::TASK_STATUS_DEADLINE_EXCEEDED,
                       "the remote call timed out", true);
    }
  }

  rescheduleDeadlines();
}

void PyFunctionTaskHandler::rescheduleDeadlines() {
  std::optional<absl::Time> earliest;

  auto consider = [&earliest](absl::Time deadline) {
    if (deadline == absl::InfinitePast()) {
      return;
    }
    if (!earliest.has_value() || deadline < *earliest) {
      earliest = deadline;
    }
  };

  for (const auto& slot : slots_) {
    if (slot->state_ == Slot::State::kStarting) {
      consider(slot->startup_deadline_);
    }
    if (slot->state_ == Slot::State::kRunning) {
      consider(slot->task_deadline_);
    }
    if (slot->state_ == Slot::State::kDead) {
      consider(slot->respawn_deadline_);
    }
    for (const auto& [call_id, deadline] : slot->remote_calls_) {
      consider(deadline);
    }
  }

  if (earliest.has_value()) {
    timer_.Schedule(*earliest);
  } else {
    timer_.Cancel();
  }
}

void PyFunctionTaskHandler::ProcessCommand(event::Command cmd) {
  if (cmd.type_ != event::Command::DEFERRED_DELETE || cmd.args_ == nullptr) {
    return;
  }
  auto* worker = static_cast<WorkerProcess*>(cmd.args_);
  auto iter = std::find_if(retired_workers_.begin(), retired_workers_.end(),
                           [worker](const std::unique_ptr<WorkerProcess>& candidate) {
                             return candidate.get() == worker;
                           });
  if (iter != retired_workers_.end()) {
    retired_workers_.erase(iter);
  }
}

auto PyFunctionTaskHandlerFactory::Name() const -> std::string { return "pyfunction"; }

auto PyFunctionTaskHandlerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<ConfigProto>();
}

auto PyFunctionTaskHandlerFactory::Create(const ::google::protobuf::Message& config,
                                          const TaskHandlerDeps& deps) -> TaskHandlerPtr {
  auto parsed = ParsePyFunctionConfig(config);
  if (!parsed.ok()) {
    strij::logging::write_stderr_fallback(strij::logging::LogEntry::ERROR,
                                          std::source_location::current(), "pyfunction handler: {}",
                                          parsed.status().message());
    return nullptr;
  }

  return std::make_unique<PyFunctionTaskHandler>(deps.dispatcher_, deps.child_task_submitter_,
                                                 *std::move(parsed));
}

auto PyFunctionTaskHandlerFactory::ParseConfig(const ::google::protobuf::Message& config)
    -> absl::StatusOr<node::HandlerCapacity> {
  auto parsed = ParsePyFunctionConfig(config);
  if (!parsed.ok()) {
    return parsed.status();
  }

  node::HandlerCapacity capacity;
  capacity.set_concurrency(parsed->concurrency_);

  return capacity;
}

} // namespace strij::nodeagent::task_handlers::pyfunction

REGISTER_FACTORY_FULLY_QUALIFIED(
    strij::nodeagent::task_handlers::pyfunction::PyFunctionTaskHandlerFactory,
    strij::nodeagent::TaskHandlerFactory, pyfunction_task_handler_registrar)
