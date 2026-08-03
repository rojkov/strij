#include "core/nodeagent/result_sender.hh"

#include <span>
#include <string>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/task/task.pb.h"

namespace strij::nodeagent {

ConnectionResultSender::ConnectionResultSender(strij::io::Connection& conn) : conn_(conn) {}

void ConnectionResultSender::Send(strij::task::TaskResult result) {
  std::string serialized;
  result.SerializeToString(&serialized);
  auto frame = strij::io::SerializeTlvFrame(
      strij::io::TlvFrame::kResult,
      std::as_bytes(std::span(serialized.data(), serialized.size())));
  conn_.Write(frame);
}

} // namespace strij::nodeagent
