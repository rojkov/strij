#include "src/core/io/llhttp_parser.hh"

#include <bit>
#include <string_view>

#include "core/logging/log.hh"

namespace carrot::io {

LlhttpParser::LlhttpParser(
    std::function<void(event::IOObject*, std::span<std::byte>)>&& on_next_read_ready,
    std::function<void()>&& on_end_of_stream,
    std::function<void(std::span<const std::byte>)>&& on_request)
    : on_next_read_ready_{std::move(on_next_read_ready)},
      on_end_of_stream_{std::move(on_end_of_stream)}, on_request_{std::move(on_request)},
      active_chunk_{std::make_unique<Chunk>()} {
  llhttp_settings_init(&settings_);
  settings_.on_body = on_body;
  settings_.on_message_complete = on_message_complete;
  llhttp_init(&parser_, HTTP_REQUEST, &settings_);
  parser_.data = this;
  on_next_read_ready_(this, readBuffer());
}

void LlhttpParser::HandleCompletion(int res, uint32_t /*flags*/) {
  if (write_in_flight_) {
    write_in_flight_ = false;
    on_end_of_stream_();
    return;
  }

  if (res > 0) {
    const size_t offset = active_chunk_->WriteCursor();
    parse(offset, res);
    active_chunk_->AdvanceCursor(res);

    if (is_message_complete_) {
      finalizeMessage();
    } else {
      on_next_read_ready_(this, readBuffer());
    }
  } else {
    on_end_of_stream_();
  }
}

void LlhttpParser::parse(const size_t offset, size_t length) {
  assert(active_chunk_ != nullptr);
  const char* data = std::bit_cast<const char*>(active_chunk_->Data().subspan(offset).data());
  LOG_DEBUG("Parse({})\n{}", length, std::string{data, length});
  const auto err = llhttp_execute(&parser_, data, length);
  assert(err == HPE_OK);
  LOG_DEBUG("Successfully parsed one chunk");
}

auto LlhttpParser::readBuffer() -> std::span<std::byte> {
  LOG_DEBUG("LlhttpParser::ReadBuffer");
  if (active_chunk_->IsFull()) {
    body_chunks_.push_back(std::move(active_chunk_));
    active_chunk_ = std::make_unique<Chunk>();
  }

  return active_chunk_->WritableSpan();
}

void LlhttpParser::finalizeMessage() {
  // No body data at all — deliver an empty request body.
  if (!active_chunk_->HasBodies() && body_chunks_.empty()) {
    on_request_({});
    return;
  }

  // Collect body data from active_chunk_ followed by previously pushed chunks.
  size_t total_size = 0;
  if (active_chunk_->HasBodies()) {
    for (const auto& body_span : active_chunk_->GetBodies()) {
      total_size += body_span.size;
    }
  }
  for (const auto& chunk : body_chunks_) {
    for (const auto& body_span : chunk->GetBodies()) {
      total_size += body_span.size;
    }
  }

  // The entire body fits in the current chunk as a single contiguous span —
  // pass it through without copying.
  if (body_chunks_.empty() && active_chunk_->GetBodies().size() == 1) {
    const auto& body_span = active_chunk_->GetBodies().front();
    on_request_(std::as_bytes(active_chunk_->Data().subspan(body_span.start, body_span.size)));
    return;
  }

  // The entire body was pushed into one full chunk as a single contiguous span
  // and the active chunk is empty — pass it through without copying.
  if (body_chunks_.size() == 1 && body_chunks_.front()->GetBodies().size() == 1 &&
      !active_chunk_->HasBodies()) {
    const auto& body_span = body_chunks_.front()->GetBodies().front();
    on_request_(
        std::as_bytes(body_chunks_.front()->Data().subspan(body_span.start, body_span.size)));
    body_chunks_.clear();
    return;
  }

  // Body is split across multiple chunks or body descriptor segments —
  // concatenate everything into a single contiguous buffer.
  std::vector<std::byte> body;
  body.reserve(total_size);
  for (const auto& chunk : body_chunks_) {
    for (const auto& body_span : chunk->GetBodies()) {
      body.append_range(chunk->Data().subspan(body_span.start, body_span.size));
    }
  }
  if (active_chunk_->HasBodies()) {
    for (const auto& body_span : active_chunk_->GetBodies()) {
      body.append_range(active_chunk_->Data().subspan(body_span.start, body_span.size));
    }
  }
  on_request_(body);
  body_chunks_.clear();
}

auto LlhttpParser::onBody(llhttp_t* /*parser*/, const char* ptr, size_t length) -> int {
  auto body = std::string_view{ptr, length};
  LOG_DEBUG("LlhttpParser::onBody {}", body);
  auto bytes = std::as_bytes(std::span<const char>{ptr, length});
  const auto& chunk_data = active_chunk_->Data();
  assert(chunk_data.data() <= bytes.data());
  assert(chunk_data.size() >= length);
  assert(bytes.data() < std::next(chunk_data.data(), chunk_data.size()));
  active_chunk_->AddBody(bytes.data(), bytes.size());
  return 0;
}

auto LlhttpParser::onMessageComplete(llhttp_t* /*parser*/) -> int {
  LOG_DEBUG("LlhttpParser::onMessageComplete");
  is_message_complete_ = true;
  return 0;
}

auto LlhttpParser::on_body(llhttp_t* parser, const char* ptr, size_t length) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onBody(parser, ptr, length);
}

auto LlhttpParser::on_message_complete(llhttp_t* parser) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onMessageComplete(parser);
}

} // namespace carrot::io
