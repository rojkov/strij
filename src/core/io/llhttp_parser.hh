#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <span>

#include "carrot/event/io_object.hh"
#include "llhttp.h"

namespace carrot::io {

class Chunk final {
public:
  auto Data() -> std::span<std::byte> { return data_; }
  void SetBody(const std::byte* start, size_t size) {
    body_start_ = start - data_.data();
    body_size_ = size;
  };
  auto GetBody() -> std::span<std::byte> {
    return {std::next(data_.data(), body_start_), body_size_};
  }

private:
  std::array<std::byte, 4096> data_{};
  uint32_t body_start_{0};
  size_t body_size_{0};
};

using ChunkPtr = std::unique_ptr<Chunk>;

class LlhttpParser : public event::IOObject {
public:
  LlhttpParser(std::function<void(event::IOObject*, std::span<std::byte>)>&& on_next_read_ready,
               std::function<void()>&& on_end_of_stream,
               std::function<void(std::span<const std::byte>)>&& on_request);
  ~LlhttpParser() override;

  // IOObject interface
  void HandleCompletion(int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override {}

private:
  static auto on_body(llhttp_t* parser, const char* at, size_t length) -> int;
  static auto on_message_complete(llhttp_t* parser) -> int;

  auto readBuffer() -> std::span<std::byte>;
  void Parse(size_t length);
  auto onBody(llhttp_t* parser, const char* at, size_t length) -> int;
  auto onMessageComplete(llhttp_t* parser) -> int;

  // TODO: These two callbacks may become a part of ReadFacilitator interface
  // (currently provided by the io::Connection class).
  std::function<void(event::IOObject* reader, std::span<std::byte> buf)> on_next_read_ready_;
  std::function<void()> on_end_of_stream_;
  std::function<void(std::span<const std::byte>)> on_request_;

  llhttp_t parser_;
  llhttp_settings_t settings_;
  ChunkPtr active_chunk_;
  bool is_message_complete_{false}; // TODO: not used?
  std::vector<ChunkPtr> body_chunks_;
};

} // namespace carrot::io
