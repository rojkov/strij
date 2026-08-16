#include "core/gateway/http_result_receiver.hh"

#include <format>
#include <string_view>

namespace strij::gateway {

namespace {

auto toBytes(std::string_view text) -> std::vector<std::byte> {
  auto span = std::as_bytes(std::span<const char>(text.data(), text.size()));
  return {span.begin(), span.end()};
}

auto chunkFrame(std::span<const std::byte> body) -> std::vector<std::byte> {
  auto size_hex = std::format("{:x}\r\n", body.size());
  auto hex_bytes = toBytes(size_hex);
  auto crlf = toBytes("\r\n");
  std::vector<std::byte> frame;
  frame.reserve(hex_bytes.size() + body.size() + crlf.size());
  frame.insert(frame.end(), hex_bytes.begin(), hex_bytes.end());
  frame.insert(frame.end(), body.begin(), body.end());
  frame.insert(frame.end(), crlf.begin(), crlf.end());
  return frame;
}

} // namespace

auto HttpResponseFramer::Next(std::span<const std::byte> body, bool is_final)
    -> std::vector<std::vector<std::byte>> {
  std::vector<std::vector<std::byte>> frames;
  switch (state_) {
  case State::kIdle:
    if (is_final) {
      auto header = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: "
                                "text/plain\r\nConnection: close\r\n\r\n",
                                body.size());
      auto header_bytes = toBytes(header);
      std::vector<std::byte> frame;
      frame.reserve(header_bytes.size() + body.size());
      frame.insert(frame.end(), header_bytes.begin(), header_bytes.end());
      frame.insert(frame.end(), body.begin(), body.end());
      frames.push_back(std::move(frame));
      state_ = State::kDone;
    } else {
      frames.push_back(toBytes("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Type: "
                               "text/plain\r\nConnection: close\r\n\r\n"));
      frames.push_back(chunkFrame(body));
      state_ = State::kChunked;
    }
    break;
  case State::kChunked:
    frames.push_back(chunkFrame(body));
    if (is_final) {
      frames.push_back(toBytes("0\r\n\r\n"));
      state_ = State::kDone;
    }
    break;
  case State::kDone:
    break;
  }
  return frames;
}

auto HttpResponseFramer::ErrorResponse(std::string_view reason)
    -> std::vector<std::vector<std::byte>> {
  std::vector<std::vector<std::byte>> frames;
  if (state_ != State::kIdle) {
    return frames;
  }

  auto header = std::format("HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\n"
                            "Content-Length: {}\r\nConnection: close\r\n\r\n",
                            reason.size());
  auto header_bytes = toBytes(header);
  std::vector<std::byte> frame;
  frame.reserve(header_bytes.size() + reason.size());
  frame.insert(frame.end(), header_bytes.begin(), header_bytes.end());
  auto reason_bytes = toBytes(reason);
  frame.insert(frame.end(), reason_bytes.begin(), reason_bytes.end());
  frames.push_back(std::move(frame));
  state_ = State::kDone;
  return frames;
}

void HttpResultReceiver::Deliver(std::span<const std::byte> value, bool is_final) {
  for (const auto& frame : framer_.Next(value, is_final)) {
    conn_.Write(frame);
  }
}

void HttpResultReceiver::DeliverError(std::string_view reason) {
  for (const auto& frame : framer_.ErrorResponse(reason)) {
    conn_.Write(frame);
  }
}

} // namespace strij::gateway
