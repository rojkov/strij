#include "core/io/gateway_http_handler.hh"

#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"

namespace carrot::io {

void GatewayHttpHandler::HandleMessage(std::span<const std::byte> msg, Connection& conn) {
  if (nodeagent_conns_.empty()) {
    auto response = std::format("HTTP/1.1 503 Service Unavailable\r\nContent-Length: "
                                "0\r\nConnection: close\r\n\r\n");
    auto response_bytes = std::as_bytes(std::span(response.data(), response.size()));
    conn.Write(response_bytes);
    return;
  }

  auto task_id = next_task_id_++;
  auto* nodeagent_conn = nodeagent_conns_[round_robin_ % nodeagent_conns_.size()];
  round_robin_++;

  auto receiver = make_receiver_(conn);
  storage_.put(task_id, std::move(receiver));

  // Build value: [task_id:8][payload:N]
  std::vector<std::byte> value;
  value.reserve(sizeof(uint64_t) + msg.size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* task_id_bytes = reinterpret_cast<const std::byte*>(&task_id);
  value.insert(value.end(), task_id_bytes, task_id_bytes + sizeof(uint64_t));
  value.insert(value.end(), msg.begin(), msg.end());

  auto frame = SerializeTlvFrame(TlvFrame::kTaskSubmission, value);
  nodeagent_conn->Write(frame);

  LOG_DEBUG("Submitted task {} to nodeagent", task_id);
}

} // namespace carrot::io
