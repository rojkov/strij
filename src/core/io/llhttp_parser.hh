#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <span>

#include "llhttp.h"

namespace carrot::io {

class Chunk final {
public:
  auto Data() -> std::span<std::byte> { return data_; }

private:
  std::array<std::byte, 4096> data_;
};

using ChunkPtr = std::unique_ptr<Chunk>;

class LlhttpParser final {
public:
  explicit LlhttpParser(std::function<void(std::span<const std::byte>)>&& on_request);
  ~LlhttpParser();

  auto ReadBuffer() -> std::span<std::byte>;
  void Parse(size_t length);

  auto onBody(llhttp_t* parser, const char* at, size_t length) -> int;
  auto onMessageComplete(llhttp_t* parser) -> int;

private:
  std::function<void(std::span<const std::byte>)> on_request_;

  llhttp_t parser_;
  llhttp_settings_t settings_;
  std::deque<ChunkPtr> chunks_;
};

} // namespace carrot::io