#include "nodeagent/core/run_task_service.hh"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "common/task/task.pb.h"
#include "nodeagent/core/admission_tracking_sender.hh"
#include "nodeagent/core/result_sender.hh"
#include "nodeagent/extensions/task_handlers/task_handlers.hh"
#include "strij/nodeagent/admission_controller.hh"

namespace strij::nodeagent {

RunTaskServiceImpl::RunTaskServiceImpl(std::shared_ptr<TaskHandlerManager> manager,
                                       std::shared_ptr<AdmissionController> admission)
    : manager_{std::move(manager)}, admission_{std::move(admission)} {}

void RunTaskServiceImpl::sendTaskRejected(io::Connection& conn, const task::Task& task,
                                          std::string_view reason) {
  task::TaskRejected rejected;
  rejected.set_id(task.id());
  rejected.set_reason(std::string(reason));
  std::string serialized;
  rejected.SerializeToString(&serialized);
  auto frame = io::SerializeTlvFrame(
      io::TlvFrame::kTaskRejected, std::as_bytes(std::span(serialized.data(), serialized.size())));
  conn.Write(frame);
  LOG_WARNING("Task {} rejected: {}", task.id(), reason);
}

void RunTaskServiceImpl::RunTask(const task::Task& task, io::Connection& conn) {
  nodeagent::TaskHandler* handler = manager_->GetHandler(task.type());
  if (handler == nullptr) {
    LOG_WARNING("No task handler for type '{}'", task.type());
    return;
  }

  const node::ResourceRequirements& requirements = task.requirements();
  const absl::Status admit_status = admission_->Admit(task.type(), requirements);
  if (!admit_status.ok()) {
    sendTaskRejected(conn, task, admit_status.message());
    return;
  }

  auto scope = std::make_unique<AdmissionScope>(admission_, task.type(), requirements);
  auto sender = std::make_unique<AdmissionTrackingSender>(
      std::make_unique<ConnectionResultSender>(conn.Mailbox()), std::move(scope));
  handler->HandleTask(task, std::move(sender));
}

} // namespace strij::nodeagent