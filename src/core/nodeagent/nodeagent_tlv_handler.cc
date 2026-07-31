#include "core/nodeagent/nodeagent_tlv_handler.hh"

#include <string>
#include <vector>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/task/task.pb.h"

namespace carrot::nodeagent {

void NodeagentTlvHandler::HandleFrame(carrot::io::TlvFrame frame, carrot::io::Connection& conn) {
  if (frame.type_id != carrot::io::TlvFrame::kTaskSubmission) {
    return; // Only handle task submissions
  }

  carrot::task::Task task;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (!task.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                           static_cast<int>(frame.value.size()))) {
    LOG_WARNING("Malformed Task frame dropped");
    return;
  }

  carrot::task::TaskResult result;
  result.set_id(task.id());
  result.set_body(task.body());

  std::string serialized;
  result.SerializeToString(&serialized);
  auto response = carrot::io::SerializeTlvFrame(
      carrot::io::TlvFrame::kResult,
      std::as_bytes(std::span(serialized.data(), serialized.size())));
  conn.Write(response);
}

} // namespace carrot::nodeagent
