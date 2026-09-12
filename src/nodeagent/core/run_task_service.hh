#pragma once

#include <memory>
#include <string_view>

#include "common/core/io/connection.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent {

// Runs admitted tasks to completion on the nodeagent event-loop thread.
// Exposed through the abstract strij::nodeagent::RunTaskService contract.
class RunTaskServiceImpl final : public RunTaskService {
public:
  explicit RunTaskServiceImpl(std::shared_ptr<TaskHandlerManager> manager,
                              std::shared_ptr<AdmissionController> admission);
  ~RunTaskServiceImpl() override = default;

  RunTaskServiceImpl(const RunTaskServiceImpl&) = delete;
  auto operator=(const RunTaskServiceImpl&) -> RunTaskServiceImpl& = delete;
  RunTaskServiceImpl(RunTaskServiceImpl&&) noexcept = delete;
  auto operator=(RunTaskServiceImpl&&) noexcept -> RunTaskServiceImpl& = delete;

  // RunTaskService
  void RunTask(const task::Task& task, io::Connection& conn) override;
  void RunTask(const task::Task& task, io::Connection& conn, AdmissionScopePtr reserved) override;
  void RunTask(const task::Task& task, ResultSenderPtr sender, AdmissionScopePtr reserved) override;
  [[nodiscard]] auto HasHandler(std::string_view type) const -> bool override;

private:
  static void sendTaskRejected(io::Connection& conn, const task::Task& task,
                               std::string_view reason);
  // Wraps `sender` in an AdmissionTrackingSender carrying `scope`, then hands
  // it to `handler` (guaranteed non-null). The scope releases on the final
  // result or on destruction.
  static void runWithHandler(const task::Task& task, nodeagent::TaskHandler* handler,
                             ResultSenderPtr sender, AdmissionScopePtr scope);

  std::shared_ptr<TaskHandlerManager> manager_;
  std::shared_ptr<AdmissionController> admission_;
};

} // namespace strij::nodeagent
