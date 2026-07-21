#pragma once

#include "core/io/connection.hh"
#include "core/io/result_receiver_storage.hh"
#include "core/io/tlv_frame.hh"

namespace carrot::io {

class GatewayTlvHandler {
public:
  explicit GatewayTlvHandler(ResultReceiverStorage& storage) : storage_{storage} {}

  void HandleFrame(TlvFrame frame, Connection& conn);

private:
  ResultReceiverStorage& storage_;
};

} // namespace carrot::io
