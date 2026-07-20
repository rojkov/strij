#pragma once

#include "core/io/message_handler.hh"

namespace carrot::io {

class TrivialEchoHandler final : public MessageHandler {
public:
  TrivialEchoHandler() = default;

  void OnMessage(std::span<const std::byte> msg, Connection& conn) override;
};

} // namespace carrot::io
