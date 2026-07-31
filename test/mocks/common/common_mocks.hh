#pragma once

#include <array>
#include <cstddef>

#include "carrot/event/command_handler.hh"
#include "core/io/protocol_parser.hh"

namespace carrot::io {

class TrivialParser final : public ProtocolParser {
public:
  auto GetReadBuffer() -> std::span<std::byte> override {
    return std::span<std::byte>(buf_.data(), buf_.size());
  }
  auto OnData(size_t /*bytes_read*/) -> Action override { return Action::NeedMoreData; }

private:
  std::array<std::byte, 128> buf_{};
};

} // namespace carrot::io

namespace carrot::event {

struct DummyOwner final : public CommandHandler {
  void ProcessCommand(Command /*cmd*/) override {}
};

} // namespace carrot::event
