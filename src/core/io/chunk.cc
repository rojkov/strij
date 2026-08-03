#include "src/core/io/chunk.hh"

namespace strij::io {

void Chunk::AddBody(const std::byte* body_start, size_t body_size) {
  bodies_.push_back({.start = static_cast<uint32_t>(body_start - data_.data()), .size = body_size});
}

} // namespace strij::io