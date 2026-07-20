#pragma once

#include "core/io/message_handler.hh"

namespace carrot::io {

class HttpEchoHandler final : public MessageHandler {
public:
  HttpEchoHandler() = default;

  void OnMessage(std::span<const std::byte> msg, Connection& conn) override;
};

} // namespace carrot::io
