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
  struct BodySpan {
    uint32_t start;
    size_t size;
  };

  auto Data() -> std::span<std::byte> { return data_; }
  auto WritableSpan() -> std::span<std::byte> {
    return {data_.data() + write_cursor_, data_.size() - write_cursor_};
  }
  auto WriteCursor() const -> size_t { return write_cursor_; }
  void AdvanceCursor(size_t n) { write_cursor_ += n; }
  void AddBody(const std::byte* body_start, size_t body_size) {
    bodies_.push_back({static_cast<uint32_t>(body_start - data_.data()), body_size});
  }
  bool HasBodies() const { return !bodies_.empty(); }
  bool IsFull() const { return write_cursor_ >= data_.size(); }
  auto GetBodies() const -> const std::vector<BodySpan>& { return bodies_; }

private:
  std::array<std::byte, 4096> data_{};
  size_t write_cursor_{0};
  std::vector<BodySpan> bodies_;
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
  void Parse(size_t offset, size_t length);
  void FinalizeMessage();
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
