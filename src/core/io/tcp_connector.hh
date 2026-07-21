#pragma once

#include <memory>
#include <string>
#include <vector>

#include "carrot/event/dispatcher.hh"
#include "core/io/connection.hh"
#include "core/io/gateway_tlv_handler.hh"
#include "core/io/tlv_parser.hh"

namespace carrot::io {

class TcpConnector : public event::IOObject {
public:
  TcpConnector(event::DispatcherSharedPtr dispatcher, ResultReceiverStorage& storage);
  ~TcpConnector() override = default;

  TcpConnector(const TcpConnector&) = delete;
  auto operator=(const TcpConnector&) -> TcpConnector& = delete;
  TcpConnector(TcpConnector&&) noexcept = delete;
  auto operator=(TcpConnector&&) noexcept -> TcpConnector& = delete;

  int Connect(const std::string& host, uint16_t port);

  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override;

private:
  event::DispatcherSharedPtr dispatcher_;
  ResultReceiverStorage& storage_;
  std::vector<std::unique_ptr<Connection>> connections_;
};

} // namespace carrot::io
