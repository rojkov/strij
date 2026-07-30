#pragma once

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"

namespace carrot::nodeagent {

class NodeagentTlvHandler {
public:
  NodeagentTlvHandler() = default;

  void HandleFrame(carrot::io::TlvFrame frame, carrot::io::Connection& conn);
};

} // namespace carrot::io
