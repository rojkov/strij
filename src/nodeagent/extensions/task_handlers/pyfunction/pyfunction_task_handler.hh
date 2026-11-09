#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/pyfunction/config.hh"
#include "nodeagent/extensions/task_handlers/pyfunction/deadline_timer.hh"
#include "nodeagent/extensions/task_handlers/pyfunction/pyfunction.pb.h"
#include "strij/event/command_handler.hh"
#include "strij/event/dispatcher.hh"
#include "strij/nodeagent/child_task_submitter.hh"
#include "strij/nodeagent/task_handlers.hh"

namespace strij::nodeagent::task_handlers::pyfunction {

// The generated config/protocol messages live in the extension's documented
// namespace (strij.extensions.task_handlers.pyfunction); the handler impl
// namespace is side-owned. Alias the wire types for readability.
using ConfigProto = ::strij::extensions::task_handlers::pyfunction::PyFunctionTaskHandlerConfig;
using InvocationMode = ::strij::extensions::task_handlers::pyfunction::InvocationMode;
using ::strij::extensions::task_handlers::pyfunction::INVOCATION_MODE_JSON;
using ::strij::extensions::task_handlers::pyfunction::INVOCATION_MODE_PICKLE;
using ::strij::extensions::task_handlers::pyfunction::INVOCATION_MODE_UNSPECIFIED;
using RemoteCall = ::strij::extensions::task_handlers::pyfunction::RemoteCall;
using RunResult = ::strij::extensions::task_handlers::pyfunction::RunResult;
using WorkerFrame = ::strij::extensions::task_handlers::pyfunction::WorkerFrame;

class WorkerProcess;
struct WorkerSpawnConfig;

// The bundled `pyfunction` handler. One slot per configured concurrency unit,
// each holding one persistent worker process. There is no task queue: an
// admitted task owns a slot until its single terminal result.
class PyFunctionTaskHandler final : public TaskHandler, public event::CommandHandler {
public:
  // Defined in the implementation unit; public so the child-result receiver can
  // hold a weak reference to it.
  struct Slot;

  PyFunctionTaskHandler(event::Dispatcher& dispatcher, ChildTaskSubmitter& submitter,
                        PyFunctionConfig config);
  ~PyFunctionTaskHandler() override;

  PyFunctionTaskHandler(const PyFunctionTaskHandler&) = delete;
  auto operator=(const PyFunctionTaskHandler&) -> PyFunctionTaskHandler& = delete;
  PyFunctionTaskHandler(PyFunctionTaskHandler&&) noexcept = delete;
  auto operator=(PyFunctionTaskHandler&&) noexcept -> PyFunctionTaskHandler& = delete;

  void HandleTask(const task::Task& task, ResultSenderPtr sender) override;
  void ProcessCommand(event::Command cmd) override;

private:
  auto makeSpawnConfig() -> WorkerSpawnConfig;
  void spawnSlot(const std::shared_ptr<Slot>& slot);
  void assignTask(const std::shared_ptr<Slot>& slot, std::string task_id, std::string body,
                  InvocationMode mode, std::string function,
                  std::map<std::string, std::string> parameters, ResultSenderPtr sender);
  void sendRun(Slot& slot);
  void handleReady(const std::shared_ptr<Slot>& slot, std::uint32_t version);
  void handleFrame(const std::shared_ptr<Slot>& slot, const WorkerFrame& frame);
  void handleRunResult(const std::shared_ptr<Slot>& slot, const RunResult& result);
  void handleRemoteCall(const std::shared_ptr<Slot>& slot, const RemoteCall& call);
  void handleWorkerExit(const std::shared_ptr<Slot>& slot, std::uint64_t generation);
  void handleDisconnect(const std::shared_ptr<Slot>& slot);
  void finishTask(const std::shared_ptr<Slot>& slot, std::string body, task::TaskStatus status,
                  std::string reason, bool worker_healthy);
  static void deliverUnavailable(const task::Task& task, ResultSender& sender);
  void scheduleRespawn(const std::shared_ptr<Slot>& slot);
  void expireDeadlines();
  void rescheduleDeadlines();
  void retireWorker(const std::shared_ptr<Slot>& slot);

  event::Dispatcher* dispatcher_;
  ChildTaskSubmitter* submitter_;
  PyFunctionConfig config_;
  std::vector<std::shared_ptr<Slot>> slots_;
  DeadlineTimer timer_;
  std::vector<std::unique_ptr<WorkerProcess>> retired_workers_;
};

class PyFunctionTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, const TaskHandlerDeps& deps)
      -> TaskHandlerPtr override;
  auto ParseConfig(const ::google::protobuf::Message& config)
      -> absl::StatusOr<node::HandlerCapacity> override;
};

} // namespace strij::nodeagent::task_handlers::pyfunction
