#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"

namespace carrot::gateway {

class GatewayHttpHandler {
public:
  GatewayHttpHandler(NodeDirectory& node_directory, ResultReceiverStorage& storage,
                     std::unique_ptr<ResultReceiver> (*make_receiver)(carrot::io::Connection& conn))
      : node_directory_{node_directory}, storage_{storage}, make_receiver_{make_receiver} {}

  void HandleMessage(std::span<const std::byte> msg, carrot::io::Connection& conn);

private:
  NodeDirectory& node_directory_;
  ResultReceiverStorage& storage_;
  std::unique_ptr<ResultReceiver> (*make_receiver_)(carrot::io::Connection& conn);
  uint64_t next_task_id_{0};
};

} // namespace carrot::gateway
