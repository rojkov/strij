#include "core/nodeagent/nodeagent_tlv_handler.hh"

#include <utility>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/nodeagent/result_sender.hh"
#include "core/task/task.pb.h"

namespace strij::nodeagent {

NodeagentTlvHandler::NodeagentTlvHandler(std::shared_ptr<TaskHandlerManager> manager)
    : manager_(std::move(manager)) {}

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

  handler->HandleTask(task, std::make_unique<ConnectionResultSender>(conn.Mailbox()));
}

} // namespace strij::nodeagent
