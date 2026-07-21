#pragma once

#include "core/io/connection.hh"

namespace carrot::io {

class HttpEchoHandler {
public:
  HttpEchoHandler() = default;

  void HandleMessage(std::span<const std::byte> msg, Connection& conn);
};

} // namespace carrot::io
