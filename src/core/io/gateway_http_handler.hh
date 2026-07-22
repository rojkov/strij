#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "core/io/connection.hh"
#include "core/io/node_directory.hh"
#include "core/io/result_receiver_storage.hh"

namespace carrot::io {

class GatewayHttpHandler {
public:
  GatewayHttpHandler(NodeDirectory& node_directory, ResultReceiverStorage& storage,
                     std::unique_ptr<ResultReceiver> (*make_receiver)(Connection& conn))
      : node_directory_{node_directory}, storage_{storage}, make_receiver_{make_receiver} {}

  void HandleMessage(std::span<const std::byte> msg, Connection& conn);

private:
  NodeDirectory& node_directory_;
  ResultReceiverStorage& storage_;
  std::unique_ptr<ResultReceiver> (*make_receiver_)(Connection& conn);
  uint64_t next_task_id_{0};
};

} // namespace carrot::io
