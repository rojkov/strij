#pragma once

#include <memory>
#include <string_view>

#include "core/io/connection.hh"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/task_handler_manager.hh"
#include "core/task/task.pb.h"

namespace strij::nodeagent {

// Runs admitted tasks to completion on the nodeagent event-loop thread. The
// nodeagent schedulers own all inbound kTaskSubmission frames and delegate
// execution here. NodeagentFactoryContext exposes this service so scheduler
// factories can reach it without knowing the concrete handler manager.
class RunTaskService final {
public:
  RunTaskService(std::shared_ptr<TaskHandlerManager> manager,
                 std::shared_ptr<AdmissionController> admission);
  ~RunTaskService() = default;

  RunTaskService(const RunTaskService&) = delete;
  auto operator=(const RunTaskService&) -> RunTaskService& = delete;
  RunTaskService(RunTaskService&&) noexcept = delete;
  auto operator=(RunTaskService&&) noexcept -> RunTaskService& = delete;

  // Admits `task` and runs it via its configured task handler, sending results
  // back over `conn`. Sends a kTaskRejected frame on admission failure. Tasks
  // with an unknown handler type are dropped with a warning. Runs synchronously
  // on the caller's (event-loop) thread.
  void RunTask(const task::Task& task, io::Connection& conn);

private:
  static void sendTaskRejected(io::Connection& conn, const task::Task& task,
                               std::string_view reason);

  std::shared_ptr<TaskHandlerManager> manager_;
  std::shared_ptr<AdmissionController> admission_;
};

} // namespace strij::nodeagent