#include "core/nodeagent/result_sender.hh"

#include <span>
#include <string>
#include <utility>

#include "core/io/tlv_frame.hh"
#include "core/task/task.pb.h"

namespace strij::nodeagent {

ConnectionResultSender::ConnectionResultSender(std::shared_ptr<io::OutboundMailbox> mailbox)
    : mailbox_{std::move(mailbox)} {}

void ConnectionResultSender::Send(task::TaskResult result) {
  std::string serialized;
  result.SerializeToString(&serialized);
  auto frame = io::SerializeTlvFrame(
      io::TlvFrame::kResult, std::as_bytes(std::span(serialized.data(), serialized.size())));
  mailbox_->Enqueue(std::move(frame));
}

auto ConnectionResultSender::RegisterOnClose(std::move_only_function<void()> close_cb)
    -> std::size_t {
  return mailbox_->RegisterOnClose(std::move(close_cb));
}

void ConnectionResultSender::UnregisterOnClose(std::size_t token) {
  mailbox_->UnregisterOnClose(token);
}

} // namespace strij::nodeagent
