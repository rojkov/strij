#include "src/core/io/llhttp_parser.hh"

#include <string_view>

#include "core/logging/log.hh"

namespace carrot::io {

namespace {
int on_body(llhttp_t* parser, const char* at, size_t length) {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  return obj->onBody(parser, at, length);
}
int on_message_complete(llhttp_t* parser) {
  auto* obj = static_cast<LlhttpParser*>(parser->data);
  return obj->onMessageComplete(parser);
}

} // namespace

LlhttpParser::LlhttpParser(std::function<void(std::span<const std::byte>)>&& on_request)
    : on_request_{std::move(on_request)} {
  llhttp_settings_init(&settings_);
  settings_.on_body = on_body;
  settings_.on_message_complete = on_message_complete;
  llhttp_init(&parser_, HTTP_REQUEST, &settings_);
  parser_.data = this;
}

LlhttpParser::~LlhttpParser() {}

auto LlhttpParser::ReadBuffer() -> std::span<std::byte> {
  LOG_DEBUG("LlhttpParser::ReadBuffer");
  chunks_.emplace_back(std::make_unique<Chunk>());
  return chunks_.back()->Data();
}

void LlhttpParser::Parse(size_t length) {
  assert(!chunks_.empty());
  assert(chunks_.back()->Data().size() >= length);
  LOG_DEBUG("Parse({})\n{}", length,
            std::string{reinterpret_cast<char*>(chunks_.back()->Data().data()), length});
  auto* data = reinterpret_cast<char*>(chunks_.back()->Data().data());
  enum llhttp_errno err = llhttp_execute(&parser_, data, length);
  assert(err == HPE_OK);
  LOG_DEBUG("Successfully parsed one chunk"); // at {}", chunks_.back()->Data().data());
}

auto LlhttpParser::onBody(llhttp_t* parser, const char* at, size_t length) -> int {
  auto body = std::string_view{at, length};
  LOG_DEBUG("LlhttpParser::onBody {}", body);
  auto bytes = std::span<const std::byte>{reinterpret_cast<const std::byte*>(at), length};
  on_request_(bytes);
  return 0;
}

auto LlhttpParser::onMessageComplete(llhttp_t* parser) -> int {
  LOG_DEBUG("LlhttpParser::onMessageComplete");
  return 0;
}
} // namespace carrot::io