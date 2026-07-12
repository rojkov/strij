#pragma once

#include <cstddef>
#include <memory>
#include <span>

namespace carrot::io {

class Connection;

class MessageHandler {
public:
  MessageHandler() = default;
  virtual ~MessageHandler() = default;

  MessageHandler(const MessageHandler&) = delete;
  auto operator=(const MessageHandler&) -> MessageHandler& = delete;
  MessageHandler(MessageHandler&&) noexcept = delete;
  auto operator=(MessageHandler&&) noexcept -> MessageHandler& = delete;

  /**
   * @brief Called when a complete protocol message has been assembled.
   * Implementations may call conn.Write() to send a response.
   */
  virtual void OnMessage(std::span<const std::byte> msg, Connection& conn) = 0;
};

using MessageHandlerPtr = std::unique_ptr<MessageHandler>;

} // namespace carrot::io
