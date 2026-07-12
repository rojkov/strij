#pragma once

#include <array>
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
    return {std::next(data_.data(), write_cursor_), data_.size() - write_cursor_};
  }
  [[nodiscard]] auto WriteCursor() const -> size_t { return write_cursor_; }
  void AdvanceCursor(ssize_t n) { write_cursor_ += n; }
  void AddBody(const std::byte* body_start, size_t body_size) {
    bodies_.push_back(
        {.start = static_cast<uint32_t>(body_start - data_.data()), .size = body_size});
  }
  [[nodiscard]] auto HasBodies() const -> bool { return !bodies_.empty(); }
  [[nodiscard]] auto IsFull() const -> bool { return write_cursor_ >= data_.size(); }
  [[nodiscard]] auto GetBodies() const -> const std::vector<BodySpan>& { return bodies_; }

private:
  const static size_t CHUNK_SIZE{4096};
  std::array<std::byte, CHUNK_SIZE> data_{};
  ssize_t write_cursor_{0};
  std::vector<BodySpan> bodies_;
};

using ChunkPtr = std::unique_ptr<Chunk>;

class LlhttpParser : public event::IOObject {
public:
  enum class Op : uint8_t { Read = 0, Write = 1 };

  LlhttpParser(std::function<void(event::IOObject*, std::span<std::byte>)>&& on_next_read_ready,
               std::function<void()>&& on_end_of_stream,
               std::function<void(std::span<const std::byte>)>&& on_request);
  ~LlhttpParser() override = default;

  LlhttpParser(const LlhttpParser&) = delete;
  auto operator=(const LlhttpParser&) -> LlhttpParser& = delete;
  LlhttpParser(LlhttpParser&&) noexcept = delete;
  auto operator=(LlhttpParser&&) noexcept -> LlhttpParser& = delete;

  // IOObject interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override {}

private:
  static auto on_body(llhttp_t* parser, const char* ptr, size_t length) -> int;
  static auto on_message_complete(llhttp_t* parser) -> int;

  auto readBuffer() -> std::span<std::byte>;
  void parse(size_t offset, size_t length);
  void finalizeMessage();
  auto onBody(llhttp_t* parser, const char* ptr, size_t length) -> int;
  auto onMessageComplete(llhttp_t* parser) -> int;

  // TODO: These two callbacks may become a part of ReadFacilitator interface
  // (currently provided by the io::Connection class).
  std::function<void(event::IOObject* reader, std::span<std::byte> buf)> on_next_read_ready_;
  std::function<void()> on_end_of_stream_;
  std::function<void(std::span<const std::byte>)> on_request_;

  llhttp_t parser_{};
  llhttp_settings_t settings_{};
  ChunkPtr active_chunk_;
  bool is_message_complete_{false};
  std::vector<ChunkPtr> body_chunks_;
};

} // namespace carrot::io
