#pragma once

#include <array>
#include <cstddef>

#include "strij/event/command_handler.hh"
#include "core/io/protocol_parser.hh"

namespace strij::io {

class TrivialParser final : public ProtocolParser {
public:
  auto GetReadBuffer() -> std::span<std::byte> override {
    return std::span<std::byte>(buf_.data(), buf_.size());
  }
  auto OnData(size_t /*bytes_read*/) -> Action override { return Action::NeedMoreData; }

private:
  std::array<std::byte, 128> buf_{};
};

} // namespace strij::io

namespace strij::event {

struct DummyOwner final : public CommandHandler {
  void ProcessCommand(Command /*cmd*/) override {}
};

} // namespace strij::event
