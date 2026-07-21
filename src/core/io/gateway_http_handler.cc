#include "core/io/gateway_http_handler.hh"

#include <arpa/inet.h>

#include <cstring>
#include <format>
#include <string>
#include <string_view>

#include "core/io/connection.hh"
#include "core/logging/log.hh"

namespace carrot::io {

void TlvSender::SendFrame(uint8_t type_id, uint64_t task_id, std::span<const std::byte> value) {
  // Wire format: [type_id:1][length:4][task_id:8][payload:N]
  uint32_t total_length = sizeof(uint64_t) + value.size();
  uint32_t net_length = htonl(total_length);

  std::array<std::byte, 5> header{};
  header[0] = std::bit_cast<std::byte>(type_id);
  std::memcpy(header.data() + 1, &net_length, 4);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* task_id_bytes = reinterpret_cast<const std::byte*>(&task_id);

  std::vector<std::byte> frame;
  frame.reserve(5 + sizeof(uint64_t) + value.size());
  frame.insert(frame.end(), header.begin(), header.end());
  frame.insert(frame.end(), task_id_bytes, task_id_bytes + sizeof(uint64_t));
  frame.insert(frame.end(), value.begin(), value.end());

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* data = reinterpret_cast<const char*>(frame.data());
  ::write(fd_, data, frame.size());
}

void GatewayHttpHandler::HandleMessage(std::span<const std::byte> msg, Connection& conn) {
  if (senders_.empty()) {
    auto response = std::format("HTTP/1.1 503 Service Unavailable\r\nContent-Length: "
                                "0\r\nConnection: close\r\n\r\n");
    auto response_bytes = std::as_bytes(std::span(response.data(), response.size()));
    conn.Write(response_bytes);
    return;
  }

  auto task_id = next_task_id_++;
  auto* sender = senders_[round_robin_ % senders_.size()].get();
  round_robin_++;

  auto receiver = make_receiver_(conn);
  storage_.put(task_id, std::move(receiver));

  sender->SendFrame(0 /* kTaskSubmission */, task_id, msg);

  LOG_DEBUG("Submitted task {} to nodeagent", task_id);
}

} // namespace carrot::io
