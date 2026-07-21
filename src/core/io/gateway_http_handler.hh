#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/io/connection.hh"
#include "core/io/result_receiver_storage.hh"

namespace carrot::io {

class TlvSender {
public:
  explicit TlvSender(int conn_fd) : fd_{conn_fd} {}

  void SendFrame(uint8_t type_id, uint64_t task_id, std::span<const std::byte> value);

private:
  int fd_;
};

using TlvSenderPtr = std::unique_ptr<TlvSender>;

class GatewayHttpHandler {
public:
  GatewayHttpHandler(std::vector<TlvSenderPtr>& senders, ResultReceiverStorage& storage,
                     std::unique_ptr<ResultReceiver> (*make_receiver)(Connection& conn))
      : senders_{senders}, storage_{storage}, make_receiver_{make_receiver} {}

  void HandleMessage(std::span<const std::byte> msg, Connection& conn);

private:
  std::vector<TlvSenderPtr>& senders_;
  ResultReceiverStorage& storage_;
  std::unique_ptr<ResultReceiver> (*make_receiver_)(Connection& conn);
  uint64_t next_task_id_{0};
  size_t round_robin_{0};
};

} // namespace carrot::io
