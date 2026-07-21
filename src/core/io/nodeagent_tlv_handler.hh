#pragma once

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"

namespace carrot::io {

class NodeagentTlvHandler {
public:
  NodeagentTlvHandler() = default;

  void HandleFrame(TlvFrame frame, Connection& conn);
};

} // namespace carrot::io
