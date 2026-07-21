#pragma once

#include "core/io/connection.hh"

namespace carrot::io {

class TrivialEchoHandler {
public:
  TrivialEchoHandler() = default;

  void HandleMessage(std::span<const std::byte> msg, Connection& conn);
};

} // namespace carrot::io
