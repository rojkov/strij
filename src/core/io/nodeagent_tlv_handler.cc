#include "core/io/nodeagent_tlv_handler.hh"

#include <arpa/inet.h>

#include <cstring>
#include <vector>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"

namespace carrot::io {

void NodeagentTlvHandler::HandleFrame(TlvFrame frame, Connection& conn) {
  if (frame.type_id != TlvFrame::kTaskSubmission) {
    return; // Only handle task submissions
  }

  std::vector<std::byte> response;
  response.reserve(5 + frame.value.size());
  response.push_back(std::byte{TlvFrame::kResult});
  response.resize(5);
  uint32_t net_len = htonl(static_cast<uint32_t>(frame.value.size()));
  std::memcpy(response.data() + 1, &net_len, 4);
  response.insert(response.end(), frame.value.begin(), frame.value.end());

  conn.Write(response);
}

} // namespace carrot::io
