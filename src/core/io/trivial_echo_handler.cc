#include "core/io/trivial_echo_handler.hh"

#include "core/io/connection.hh"

namespace carrot::io {
void TrivialEchoHandler::HandleMessage(std::span<const std::byte> msg, Connection& conn) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  conn.Write(msg);
}
} // namespace carrot::io