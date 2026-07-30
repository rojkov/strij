#pragma once

#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"

namespace carrot::gateway {

class GatewayTlvHandler {
public:
  explicit GatewayTlvHandler(ResultReceiverStorage& storage) : storage_{storage} {}

  void HandleFrame(carrot::io::TlvFrame frame, carrot::io::Connection& conn);

private:
  ResultReceiverStorage& storage_;
};

} // namespace carrot::gateway
