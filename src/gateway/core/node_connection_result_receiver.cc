#include "gateway/core/node_connection_result_receiver.hh"

#include <bit>
#include <span>
#include <string>
#include <string_view>

#include "common/core/io/tlv_frame.hh"
#include "common/task/task.pb.h"

namespace strij::gateway {

NodeConnectionResultReceiver::NodeConnectionResultReceiver(
    std::string task_id, std::shared_ptr<io::OutboundMailbox> mailbox)
    : task_id_{std::move(task_id)}, mailbox_{std::move(mailbox)} {}

void NodeConnectionResultReceiver::Deliver(std::span<const std::byte> value, bool is_final) {
  task::TaskResult result;
  result.set_id(task_id_);
  result.set_body(std::bit_cast<const char*>(value.data()), value.size());
  // Absence of the optional is_final field means final (proto3), so a
  // non-final chunk must be marked explicitly.
  result.set_is_final(is_final);

  std::string serialized;
  result.SerializeToString(&serialized);
  mailbox_->Enqueue(io::SerializeTlvFrame(
      io::TlvFrame::kResult, std::as_bytes(std::span(serialized.data(), serialized.size()))));
}

void NodeConnectionResultReceiver::DeliverError(std::string_view reason) {
  task::TaskRejected rejected;
  rejected.set_id(task_id_);
  rejected.set_reason(std::string(reason));

  std::string serialized;
  rejected.SerializeToString(&serialized);
  mailbox_->Enqueue(io::SerializeTlvFrame(
      io::TlvFrame::kTaskRejected, std::as_bytes(std::span(serialized.data(), serialized.size()))));
}

} // namespace strij::gateway