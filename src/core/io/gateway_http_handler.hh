#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/io/connection.hh"
#include "core/io/result_receiver_storage.hh"

namespace carrot::io {

class GatewayHttpHandler {
public:
  GatewayHttpHandler(std::vector<Connection*>& nodeagent_conns, ResultReceiverStorage& storage,
                     std::unique_ptr<ResultReceiver> (*make_receiver)(Connection& conn))
      : nodeagent_conns_{nodeagent_conns}, storage_{storage}, make_receiver_{make_receiver} {}

  void HandleMessage(std::span<const std::byte> msg, Connection& conn);

private:
  std::vector<Connection*>& nodeagent_conns_;
  ResultReceiverStorage& storage_;
  std::unique_ptr<ResultReceiver> (*make_receiver_)(Connection& conn);
  uint64_t next_task_id_{0};
  size_t round_robin_{0};
};

} // namespace carrot::io
