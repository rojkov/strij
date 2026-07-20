#pragma once

#include <functional>
#include <span>

#include "core/io/chunk.hh"
#include "core/io/protocol_parser.hh"
#include "llhttp.h"

namespace carrot::io {

class LlhttpParser final : public ProtocolParser {
public:
  explicit LlhttpParser(std::function<void(std::span<const std::byte>)>&& on_message);
  ~LlhttpParser() override = default;

  LlhttpParser(const LlhttpParser&) = delete;
  auto operator=(const LlhttpParser&) -> LlhttpParser& = delete;
  LlhttpParser(LlhttpParser&&) noexcept = delete;
  auto operator=(LlhttpParser&&) noexcept -> LlhttpParser& = delete;

  // ProtocolParser interface
  auto GetReadBuffer() -> std::span<std::byte> override;
  auto OnData(size_t bytes_read) -> Action override;

private:
  static auto on_body(llhttp_t* parser, const char* ptr, size_t length) -> int;
  static auto on_message_complete(llhttp_t* parser) -> int;

  void parse(size_t offset, size_t length);
  void finalizeMessage();
  auto onBody(llhttp_t* parser, const char* ptr, size_t length) -> int;
  auto onMessageComplete(llhttp_t* parser) -> int;

  std::function<void(std::span<const std::byte>)> on_message_;

  llhttp_t parser_{};
  llhttp_settings_t settings_{};
  ChunkPtr active_chunk_;
  bool is_message_complete_{false};
  std::vector<ChunkPtr> body_chunks_;
};

} // namespace carrot::io
