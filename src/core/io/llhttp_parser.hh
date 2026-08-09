#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/io/chunk.hh"
#include "core/io/protocol_parser.hh"
#include "llhttp.h"

namespace strij::io {

struct HttpRequest {
  std::string_view path;
  std::span<const std::byte> body;
  std::vector<std::pair<std::string, std::string>> headers;
};

class LlhttpParser final : public ProtocolParser {
public:
  explicit LlhttpParser(std::move_only_function<void(const HttpRequest&)>&& on_message);
  ~LlhttpParser() override = default;

  LlhttpParser(const LlhttpParser&) = delete;
  auto operator=(const LlhttpParser&) -> LlhttpParser& = delete;
  LlhttpParser(LlhttpParser&&) noexcept = delete;
  auto operator=(LlhttpParser&&) noexcept -> LlhttpParser& = delete;

  // ProtocolParser interface
  auto GetReadBuffer() -> std::span<std::byte> override;
  auto OnData(size_t bytes_read) -> Action override;

private:
  static auto on_url(llhttp_t* parser, const char* ptr, size_t length) -> int;
  static auto on_header_field(llhttp_t* parser, const char* ptr, size_t length) -> int;
  static auto on_header_value(llhttp_t* parser, const char* ptr, size_t length) -> int;
  static auto on_header_value_complete(llhttp_t* parser) -> int;
  static auto on_headers_complete(llhttp_t* parser) -> int;
  static auto on_body(llhttp_t* parser, const char* ptr, size_t length) -> int;
  static auto on_message_complete(llhttp_t* parser) -> int;

  void parse(size_t offset, size_t length);
  void finalizeMessage();
  auto onUrl(llhttp_t* parser, const char* ptr, size_t length) -> int;
  auto onHeaderField(llhttp_t* parser, const char* ptr, size_t length) -> int;
  auto onHeaderValue(llhttp_t* parser, const char* ptr, size_t length) -> int;
  auto onHeaderValueComplete(llhttp_t* parser) -> int;
  auto onHeadersComplete(llhttp_t* parser) -> int;
  auto onBody(llhttp_t* parser, const char* ptr, size_t length) -> int;
  auto onMessageComplete(llhttp_t* parser) -> int;

  std::move_only_function<void(const HttpRequest&)> on_message_;

  llhttp_t parser_{};
  llhttp_settings_t settings_{};
  ChunkPtr active_chunk_;
  bool is_message_complete_{false};
  std::vector<ChunkPtr> body_chunks_;
  std::string path_;
  std::vector<std::pair<std::string, std::string>> headers_;
  std::string header_field_;
  std::string header_value_;
  bool header_field_pending_{false};
};

} // namespace strij::io
