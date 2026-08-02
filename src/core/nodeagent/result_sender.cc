#include "core/nodeagent/result_sender.hh"

#include <span>
#include <string>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/task/task.pb.h"

namespace carrot::nodeagent {

ConnectionResultSender::ConnectionResultSender(carrot::io::Connection& conn) : conn_(conn) {}

void ConnectionResultSender::Send(carrot::task::TaskResult result) {
  std::string serialized;
  result.SerializeToString(&serialized);
  auto frame = carrot::io::SerializeTlvFrame(
      carrot::io::TlvFrame::kResult,
      std::as_bytes(std::span(serialized.data(), serialized.size())));
  conn_.Write(frame);
}

} // namespace carrot::nodeagent
