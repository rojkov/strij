#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "core/io/llhttp_parser.hh"
#include "gtest/gtest.h"

namespace strij::io {
namespace {

struct CapturedRequest {
  std::string path;
  std::vector<std::byte> body;
  std::vector<std::pair<std::string, std::string>> headers;
};

class LlhttpParserTest : public ::testing::Test {
protected:
  // Body spans are only valid during the callback — copy the data to keep it
  // after the feed() call (matches the parser's zero-copy delivery contract).
  std::vector<CapturedRequest> received_;
  LlhttpParser parser_{[this](HttpRequest request) -> void {
    received_.push_back({std::string(request.path),
                         std::vector<std::byte>(request.body.begin(), request.body.end()),
                         std::move(request.headers)});
  }};

  void feed(std::span<const std::byte> data) {
    size_t offset = 0;
    while (offset < data.size()) {
      auto buf = parser_.GetReadBuffer();
      size_t num_to_read_from_wire = std::min(data.size() - offset, buf.size());
      std::memcpy(buf.data(), std::next(data.data(), static_cast<ssize_t>(offset)),
                  num_to_read_from_wire);
      parser_.OnData(num_to_read_from_wire);
      offset += num_to_read_from_wire;
    }
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(LlhttpParserTest, CapturesPathAndBody) {
  const std::string request =
      "POST /tasks/echo HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "hello";
  auto data = std::as_bytes(std::span(request.data(), request.size()));
  feed(data);

  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].path, "/tasks/echo");
  ASSERT_EQ(received_[0].body.size(), 5U);
  EXPECT_EQ(static_cast<char>(received_[0].body[0]), 'h');
  EXPECT_EQ(static_cast<char>(received_[0].body[4]), 'o');
}

TEST_F(LlhttpParserTest, CapturesPathWithoutBody) {
  const std::string request =
      "GET /tasks HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "\r\n";
  auto data = std::as_bytes(std::span(request.data(), request.size()));
  feed(data);

  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].path, "/tasks");
  EXPECT_TRUE(received_[0].body.empty());
}

TEST_F(LlhttpParserTest, CapturesPathWithQuery) {
  const std::string request =
      "POST /tasks/echo?param=1 HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 2\r\n"
      "\r\n"
      "ok";
  auto data = std::as_bytes(std::span(request.data(), request.size()));
  feed(data);

  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].path, "/tasks/echo?param=1");
}

TEST_F(LlhttpParserTest, PartialRequestNeedsMoreData) {
  const std::string request =
      "POST /tasks/echo HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 5\r\n"
      "\r\n";

  auto data = std::as_bytes(std::span(request.data(), request.size()));
  feed(data);
  EXPECT_TRUE(received_.empty());

  // Feed the remaining body
  const std::string body = "hello";
  feed(std::as_bytes(std::span(body.data(), body.size())));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].path, "/tasks/echo");
  ASSERT_EQ(received_[0].body.size(), 5U);
}

TEST_F(LlhttpParserTest, UrlSplitAcrossReads) {
  const std::string request =
      "POST /tasks/echo HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 0\r\n"
      "\r\n";

  auto data = std::as_bytes(std::span(request.data(), request.size()));

  // Feed one byte at a time so the URL is parsed in fragments
  for (size_t i = 0; i < data.size() - 1; ++i) {
    feed(data.subspan(i, 1));
    EXPECT_TRUE(received_.empty());
  }
  feed(data.subspan(data.size() - 1, 1));

  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].path, "/tasks/echo");
}

TEST_F(LlhttpParserTest, BodySplitAcrossReads) {
  const std::string request =
      "POST /tasks/echo HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 11\r\n"
      "\r\n"
      "hello world";

  auto data = std::as_bytes(std::span(request.data(), request.size()));

  // Feed everything but the last 3 body bytes
  feed(data.subspan(0, data.size() - 3));
  EXPECT_TRUE(received_.empty());

  feed(data.subspan(data.size() - 3));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].path, "/tasks/echo");
  ASSERT_EQ(received_[0].body.size(), 11U);
  const std::string body(reinterpret_cast<const char*>(received_[0].body.data()),
                         received_[0].body.size());
  EXPECT_EQ(body, "hello world");
}

TEST_F(LlhttpParserTest, CapturesHeaders) {
  const std::string request =
      "POST /tasks/echo HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "x-strij-function: /usr/bin/cat\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
  auto data = std::as_bytes(std::span(request.data(), request.size()));
  feed(data);

  ASSERT_EQ(received_.size(), 1U);
  ASSERT_EQ(received_[0].headers.size(), 3U);
  EXPECT_EQ(received_[0].headers[0].first, "Host");
  EXPECT_EQ(received_[0].headers[0].second, "localhost");
  EXPECT_EQ(received_[0].headers[1].first, "x-strij-function");
  EXPECT_EQ(received_[0].headers[1].second, "/usr/bin/cat");
  EXPECT_EQ(received_[0].headers[2].first, "Content-Length");
  EXPECT_EQ(received_[0].headers[2].second, "0");
}

TEST_F(LlhttpParserTest, HeaderValueSplitAcrossReads) {
  const std::string request =
      "POST /tasks/echo HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "x-strij-function: /usr/bin/cat\r\n"
      "Content-Length: 0\r\n"
      "\r\n";

  auto data = std::as_bytes(std::span(request.data(), request.size()));

  // Feed one byte at a time so header fields and values are parsed in fragments
  for (size_t i = 0; i < data.size(); ++i) {
    feed(data.subspan(i, 1));
  }

  ASSERT_EQ(received_.size(), 1U);
  ASSERT_EQ(received_[0].headers.size(), 3U);
  EXPECT_EQ(received_[0].headers[1].first, "x-strij-function");
  EXPECT_EQ(received_[0].headers[1].second, "/usr/bin/cat");
}

TEST_F(LlhttpParserTest, EmptyHeaderValueCaptured) {
  const std::string request =
      "POST /tasks/echo HTTP/1.1\r\n"
      "x-strij-empty:\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
  auto data = std::as_bytes(std::span(request.data(), request.size()));
  feed(data);

  ASSERT_EQ(received_.size(), 1U);
  ASSERT_EQ(received_[0].headers.size(), 2U);
  EXPECT_EQ(received_[0].headers[0].first, "x-strij-empty");
  EXPECT_TRUE(received_[0].headers[0].second.empty());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::io
