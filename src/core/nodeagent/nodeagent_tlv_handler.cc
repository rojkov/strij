#include "core/nodeagent/nodeagent_tlv_handler.hh"

#include <vector>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"

namespace carrot::nodeagent {

void NodeagentTlvHandler::HandleFrame(carrot::io::TlvFrame frame, carrot::io::Connection& conn) {
  if (frame.type_id != carrot::io::TlvFrame::kTaskSubmission) {
    return; // Only handle task submissions
  }

  auto response = carrot::io::SerializeTlvFrame(carrot::io::TlvFrame::kResult, frame.value);
  conn.Write(response);
}

} // namespace carrot::nodeagent
