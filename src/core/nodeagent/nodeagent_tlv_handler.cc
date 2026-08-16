#include "core/nodeagent/nodeagent_tlv_handler.hh"

#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/node/capabilities.pb.h"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/admission_tracking_sender.hh"
#include "core/nodeagent/result_sender.hh"
#include "core/task/task.pb.h"

namespace strij::nodeagent {

NodeagentTlvHandler::NodeagentTlvHandler(std::shared_ptr<TaskHandlerManager> manager,
                                         std::shared_ptr<const node::NodeCapabilities> capabilities,
                                         std::shared_ptr<AdmissionController> admission)
    : manager_{std::move(manager)}, capabilities_{std::move(capabilities)},
      admission_{std::move(admission)} {}

void NodeagentTlvHandler::SendAdvertisement(io::Connection& conn) {
  std::string serialized;
  capabilities_->SerializeToString(&serialized);
  auto frame =
      io::SerializeTlvFrame(io::TlvFrame::kNodeAdvertisement,
                            std::as_bytes(std::span(serialized.data(), serialized.size())));
  conn.Write(frame);
}

auto NodeagentTlvHandler::resolveRequirements(const std::string& task_type) const
    -> node::ResourceRequirements {
  for (const auto& handler : capabilities_->handlers()) {
    if (handler.task_type() == task_type && handler.has_default_resources()) {
      return handler.default_resources();
    }
  }
  return {};
}

void NodeagentTlvHandler::sendTaskRejected(io::Connection& conn, const std::string& task_id,
                                           std::string_view reason) {
  task::TaskRejected rejected;
  rejected.set_id(task_id);
  rejected.set_reason(std::string(reason));
  std::string serialized;
  rejected.SerializeToString(&serialized);
  auto frame = io::SerializeTlvFrame(
      io::TlvFrame::kTaskRejected, std::as_bytes(std::span(serialized.data(), serialized.size())));
  conn.Write(frame);
  LOG_WARNING("Task {} rejected: {}", task_id, reason);
}

void NodeagentTlvHandler::HandleFrame(io::TlvFrame frame, io::Connection& conn) {
  if (frame.type_id != io::TlvFrame::kTaskSubmission) {
    return;
  }

  task::Task task;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (!task.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                           static_cast<int>(frame.value.size()))) {
    LOG_WARNING("Malformed Task frame dropped");
    return;
  }

  extensions::TaskHandler* handler = manager_->GetHandler(task.type());
  if (handler == nullptr) {
    LOG_WARNING("No task handler for type '{}'", task.type());
    return;
  }

  const node::ResourceRequirements requirements = resolveRequirements(task.type());
  const absl::Status admit_status = admission_->Admit(task.type(), requirements);
  if (!admit_status.ok()) {
    sendTaskRejected(conn, task.id(), admit_status.message());
    return;
  }

  auto scope = std::make_unique<AdmissionScope>(admission_, task.type(), requirements);
  auto sender = std::make_unique<AdmissionTrackingSender>(
      std::make_unique<ConnectionResultSender>(conn.Mailbox()), std::move(scope));
  handler->HandleTask(task, std::move(sender));
}

} // namespace strij::nodeagent
