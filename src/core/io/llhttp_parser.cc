#include "src/core/io/llhttp_parser.hh"

#include <bit>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/logging/log.hh"

namespace strij::io {

LlhttpParser::LlhttpParser(std::move_only_function<void(HttpRequest)>&& on_message)
    : on_message_{std::move(on_message)}, active_chunk_{std::make_unique<Chunk>()} {
  llhttp_settings_init(&settings_);
  settings_.on_url = on_url;
  settings_.on_header_field = on_header_field;
  settings_.on_header_value = on_header_value;
  settings_.on_header_value_complete = on_header_value_complete;
  settings_.on_headers_complete = on_headers_complete;
  settings_.on_body = on_body;
  settings_.on_message_complete = on_message_complete;
  llhttp_init(&parser_, HTTP_REQUEST, &settings_);
  parser_.data = this;
}

auto LlhttpParser::GetReadBuffer() -> std::span<std::byte> {
  if (active_chunk_->IsFull()) {
    body_chunks_.push_back(std::move(active_chunk_));
    active_chunk_ = std::make_unique<Chunk>();
  }
  return active_chunk_->WritableSpan();
}

auto LlhttpParser::OnData(size_t bytes_read) -> Action {
  const size_t offset = active_chunk_->WriteCursor();
  parse(offset, bytes_read);
  active_chunk_->AdvanceCursor(bytes_read);

  if (is_message_complete_) {
    finalizeMessage();
    return Action::MessageComplete;
  }
  return Action::NeedMoreData;
}

void LlhttpParser::parse(const size_t offset, size_t length) {
  assert(active_chunk_ != nullptr);
  const char* data = std::bit_cast<const char*>(active_chunk_->Data().subspan(offset).data());
  LOG_DEBUG("Parse({})\n{}", length, std::string{data, length});
  const auto err = llhttp_execute(&parser_, data, length);
  assert(err == HPE_OK);
  LOG_DEBUG("Successfully parsed one chunk");
}

void LlhttpParser::finalizeMessage() {
  // No body data at all — deliver an empty request body.
  if (!active_chunk_->HasBodies() && body_chunks_.empty()) {
    on_message_({.path = path_, .body = {}, .headers = std::move(headers_)});
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
    on_message_(
        {.path = path_,
         .body = std::as_bytes(active_chunk_->Data().subspan(body_span.start, body_span.size)),
         .headers = std::move(headers_)});
    return;
  }

  // The entire body was pushed into one full chunk as a single contiguous span
  // and the active chunk is empty — pass it through without copying.
  if (body_chunks_.size() == 1 && body_chunks_.front()->GetBodies().size() == 1 &&
      !active_chunk_->HasBodies()) {
    const auto& body_span = body_chunks_.front()->GetBodies().front();
    on_message_({.path = path_,
                 .body = std::as_bytes(
                     body_chunks_.front()->Data().subspan(body_span.start, body_span.size)),
                 .headers = std::move(headers_)});
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
  on_message_({.path = path_, .body = body, .headers = std::move(headers_)});
  body_chunks_.clear();
}

auto LlhttpParser::onUrl(llhttp_t* /*parser*/, const char* ptr, size_t length) -> int {
  LOG_DEBUG("LlhttpParser::onUrl {}", std::string_view{ptr, length});
  path_.append(ptr, length);
  return 0;
}

auto LlhttpParser::onHeaderField(llhttp_t* /*parser*/, const char* ptr, size_t length) -> int {
  // A single field name may arrive across multiple calls (fragmented reads) —
  // accumulate; the pair is flushed once the value completes.
  header_field_.append(ptr, length);
  header_field_pending_ = true;
  return 0;
}

auto LlhttpParser::onHeaderValue(llhttp_t* /*parser*/, const char* ptr, size_t length) -> int {
  header_value_.append(ptr, length);
  return 0;
}

auto LlhttpParser::onHeaderValueComplete(llhttp_t* /*parser*/) -> int {
  if (header_field_pending_) {
    headers_.emplace_back(std::move(header_field_), std::move(header_value_));
    header_field_.clear();
    header_value_.clear();
    header_field_pending_ = false;
  }
  return 0;
}

auto LlhttpParser::onHeadersComplete(llhttp_t* /*parser*/) -> int {
  // Fallback flush in case the final value never completes via the callback.
  if (header_field_pending_) {
    headers_.emplace_back(std::move(header_field_), std::move(header_value_));
    header_field_.clear();
    header_value_.clear();
    header_field_pending_ = false;
  }
  return 0;
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

auto LlhttpParser::on_url(llhttp_t* parser, const char* ptr, size_t length) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onUrl(parser, ptr, length);
}

auto LlhttpParser::on_header_field(llhttp_t* parser, const char* ptr, size_t length) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onHeaderField(parser, ptr, length);
}

auto LlhttpParser::on_header_value(llhttp_t* parser, const char* ptr, size_t length) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onHeaderValue(parser, ptr, length);
}

auto LlhttpParser::on_header_value_complete(llhttp_t* parser) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onHeaderValueComplete(parser);
}

auto LlhttpParser::on_headers_complete(llhttp_t* parser) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onHeadersComplete(parser);
}

auto LlhttpParser::on_message_complete(llhttp_t* parser) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onMessageComplete(parser);
}

} // namespace strij::io
