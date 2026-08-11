#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "core/io/outbound_mailbox.hh"
#include "core/io/protocol_parser.hh"
#include "strij/event/command_handler.hh"
#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"

namespace strij::io {

class Connection;

using ConnectionFactory = std::function<std::unique_ptr<ProtocolParser>(Connection& conn)>;

class Connection final : public event::Completable {
public:
  Connection(int connection_fd, event::DispatcherSharedPtr dispatcher, event::CommandHandler* owner,
             const ConnectionFactory& factory);
  ~Connection() override;

  Connection(const Connection&) = delete;
  auto operator=(const Connection&) -> Connection& = delete;
  Connection(Connection&&) noexcept = delete;
  auto operator=(Connection&&) noexcept -> Connection& = delete;

  // Completable interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;

  void Write(std::span<const std::byte> data);
  auto Mailbox() -> std::shared_ptr<OutboundMailbox>;
  // Returns the CommandHandler that owns this connection (e.g. the gateway
  // Node for a nodeagent connection, or the TcpListener owner).
  auto GetOwner() -> event::CommandHandler* { return owner_; }
  // Synchronously closes the fd and the outbound mailbox. Used when the owner
  // tears the connection down directly (e.g. node removal); the deferred
  // CLOSE_CONNECTION command is not submitted.
  void Close();

private:
  enum Tags : uint8_t { kRead = 0, kWrite = 1 };

  void onEndOfStream();

  int fd_;
  event::DispatcherSharedPtr dispatcher_;
  event::CommandHandler* owner_;
  std::unique_ptr<ProtocolParser> parser_;
  std::shared_ptr<OutboundMailbox> mailbox_;
  std::deque<std::vector<std::byte>> write_queue_;
  size_t write_offset_{0};
};

} // namespace strij::io
