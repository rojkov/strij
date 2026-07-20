#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace carrot::io {

const static size_t CHUNK_SIZE{4096};

class Chunk final {
public:
  struct BodySpan {
    uint32_t start;
    size_t size;
  };

  auto Data() -> std::span<std::byte> { return data_; }
  auto WritableSpan() -> std::span<std::byte> {
    return {std::next(data_.data(), static_cast<ssize_t>(write_cursor_)),
            data_.size() - write_cursor_};
  }
  [[nodiscard]] auto WriteCursor() const -> size_t { return write_cursor_; }
  void AdvanceCursor(size_t n) { write_cursor_ += n; }
  void AddBody(const std::byte* body_start, size_t body_size);

  [[nodiscard]] auto HasBodies() const -> bool { return !bodies_.empty(); }
  [[nodiscard]] auto IsFull() const -> bool { return write_cursor_ >= data_.size(); }
  [[nodiscard]] auto GetBodies() const -> const std::vector<BodySpan>& { return bodies_; }

private:
  std::array<std::byte, CHUNK_SIZE> data_{};
  size_t write_cursor_{0};
  std::vector<BodySpan> bodies_;
};

using ChunkPtr = std::unique_ptr<Chunk>;

} // namespace carrot::io