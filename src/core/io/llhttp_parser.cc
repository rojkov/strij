#include "src/core/io/llhttp_parser.hh"

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

LlhttpParser::~LlhttpParser() {}

// PLAN:
// 1. Hide ReadBuffer()
// 2. Make LlhttpParser's constructor accept a reference to Dispatcher to
//    do PrepareRead(). Connection is interested only signals about updates
//    in a multi-chunked (multi-sliced) input buffer.
// 3. A parser should request a span (a slice) from the buffer,
//    PrepareRead() it, store a pointer to it until HandleCompletion() happens,
//    then parse it when HandleCompletion() actually happens.
// 4. Consider the case when a part of body has been read, but the message is
//    not complete. The connection should know what to do with the received
//    data: either stream it farther chunk by chunk and drain the buffer after
//    every read completion or wait until the whole message has been received
//    and parsed. In this case the connection consumes the message at once
//    (optionally after linearization of the buffer).

void LlhttpParser::HandleCompletion(int res, uint32_t flags) {
  if (res > 0) {
    Parse(res);
    on_next_read_ready_(this, readBuffer());
  } else {
    on_end_of_stream_();
  }
}

void LlhttpParser::Parse(size_t length) {
  assert(active_chunk_ != nullptr);
  LOG_DEBUG("Parse({})\n{}", length,
            std::string{reinterpret_cast<char*>(active_chunk_->Data().data()), length});
  auto* data = reinterpret_cast<char*>(active_chunk_->Data().data());
  enum llhttp_errno err = llhttp_execute(&parser_, data, length);
  assert(err == HPE_OK);
  LOG_DEBUG("Successfully parsed one chunk");
  if (!is_message_complete_) {
  }
}

auto LlhttpParser::readBuffer() -> std::span<std::byte> {
  LOG_DEBUG("LlhttpParser::ReadBuffer");
  return active_chunk_->Data();
}

auto LlhttpParser::onBody(llhttp_t* parser, const char* at, size_t length) -> int {
  auto body = std::string_view{at, length};
  LOG_DEBUG("LlhttpParser::onBody {}", body);
  auto bytes = std::span<const std::byte>{reinterpret_cast<const std::byte*>(at), length};
  assert(active_chunk_->Data().data() <= bytes.data());
  assert(active_chunk_->Data().size() >= length);
  assert(bytes.data() < active_chunk_->Data().data() + active_chunk_->Data().size());
  active_chunk_->SetBody(reinterpret_cast<const std::byte*>(at), length);
  // TODO: can we reuse active_chunk_ if it is not full yet.
  body_chunks_.push_back(std::move(active_chunk_));
  // TODO: can we postpone chunk creation until dispatcher's PrepareRead()?
  active_chunk_ = std::make_unique<Chunk>();
  return 0;
}

auto LlhttpParser::onMessageComplete(llhttp_t* parser) -> int {
  LOG_DEBUG("LlhttpParser::onMessageComplete");
  is_message_complete_ = true;
  if (body_chunks_.size() == 1) {
    on_request_(body_chunks_.front()->GetBody());
  } else if (body_chunks_.size() > 1) {
    size_t total_size{0};
    for (const auto& chunk : body_chunks_) {
      total_size += chunk->GetBody().size();
    }
    std::vector<std::byte> body;
    body.reserve(total_size);
    for (const auto& chunk : body_chunks_) {
      body.append_range(chunk->GetBody());
    }
  }
  return 0;
}

auto LlhttpParser::on_body(llhttp_t* parser, const char* at, size_t length) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onBody(parser, at, length);
}

auto LlhttpParser::on_message_complete(llhttp_t* parser) -> int {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  assert(obj != nullptr);
  return obj->onMessageComplete(parser);
}

} // namespace carrot::io
