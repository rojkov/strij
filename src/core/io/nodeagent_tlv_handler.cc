#include "core/io/nodeagent_tlv_handler.hh"

#include <vector>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"

namespace carrot::io {

void NodeagentTlvHandler::HandleFrame(TlvFrame frame, Connection& conn) {
  if (frame.type_id != TlvFrame::kTaskSubmission) {
    return; // Only handle task submissions
  }

  auto response = SerializeTlvFrame(TlvFrame::kResult, frame.value);
  conn.Write(response);
}

} // namespace carrot::io
