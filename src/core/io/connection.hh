#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"
#include "core/io/protocol_parser.hh"

namespace carrot::io {

class Connection;

using ConnectionFactory = std::function<std::unique_ptr<ProtocolParser>(Connection& conn)>;

class Connection final : public event::IOObject {
public:
  Connection(int connection_fd, event::DispatcherSharedPtr dispatcher, event::IOObject* owner,
             ConnectionFactory factory);
  ~Connection() override = default;

  Connection(const Connection&) = delete;
  auto operator=(const Connection&) -> Connection& = delete;
  Connection(Connection&&) noexcept = delete;
  auto operator=(Connection&&) noexcept -> Connection& = delete;

  // IOObject interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  void ProcessCommand(event::Command /*cmd*/) override {}

  void Write(std::span<const std::byte> data);

private:
  enum Tags : uint8_t { kRead = 0, kWrite = 1 };

  void onEndOfStream();

  int fd_;
  event::DispatcherSharedPtr dispatcher_;
  event::IOObject* owner_;
  std::unique_ptr<ProtocolParser> parser_;
  std::vector<std::byte> write_buf_;
  size_t write_offset_{0};
};

} // namespace carrot::io
