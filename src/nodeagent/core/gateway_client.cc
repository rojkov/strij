#include "nodeagent/core/gateway_client.hh"

#include <span>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/task/task.pb.h"

namespace strij::nodeagent {

auto GatewayClient::Forward(const task::Task& task) -> absl::Status {
  io::OutboundMailbox* mailbox = connections_.Next();
  if (mailbox == nullptr) {
    return absl::UnavailableError("no live gateway connection to forward child task '" + task.id() +
                                  "'");
  }

  std::string serialized;
  if (!task.SerializeToString(&serialized)) {
    return absl::InternalError("failed to serialize child task '" + task.id() + "' for forwarding");
  }

  auto frame =
      io::SerializeTlvFrame(io::TlvFrame::kTaskSubmission,
                            std::as_bytes(std::span(serialized.data(), serialized.size())));
  mailbox->Enqueue(std::move(frame));
  return absl::OkStatus();
}

void GatewayClient::RegisterConnection(std::shared_ptr<io::OutboundMailbox> mailbox) {
  connections_.Register(std::move(mailbox));
}

auto GatewayClient::LiveCount() const -> size_t { return connections_.LiveCount(); }

} // namespace strij::nodeagent
