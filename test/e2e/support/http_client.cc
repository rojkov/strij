#include "test/e2e/support/http_client.hh"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace strij::e2e {

namespace {

constexpr std::size_t kReadChunkSize = 4096;

auto lower(std::string_view text) -> std::string {
  std::string result(text);
  std::ranges::transform(result, result.begin(), [](unsigned char chr) -> unsigned char {
    return static_cast<unsigned char>(std::tolower(chr));
  });
  return result;
}

auto trim(std::string_view text) -> std::string_view {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
    ++begin;
  }
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

auto decodeChunked(std::string_view encoded) -> absl::StatusOr<std::string> {
  std::string decoded;
  std::size_t position = 0;
  for (;;) {
    const std::size_t line_end = encoded.find("\r\n", position);
    if (line_end == std::string_view::npos) {
      return absl::InternalError("malformed chunked response: missing chunk size line");
    }

    const std::string_view size_text = trim(encoded.substr(position, line_end - position));
    std::size_t size = 0;
    try {
      size = static_cast<std::size_t>(std::stoull(std::string(size_text), nullptr, 16));
    } catch (const std::exception&) {
      return absl::InternalError(
          absl::StrCat("malformed chunked response: invalid chunk size '", size_text, "'"));
    }

    position = line_end + 2;
    if (size == 0) {
      return decoded;
    }
    if (position + size > encoded.size()) {
      return absl::InternalError("malformed chunked response: truncated chunk body");
    }
    decoded.append(encoded.substr(position, size));
    position += size;
    if (position + 2 > encoded.size() || encoded.substr(position, 2) != "\r\n") {
      return absl::InternalError("malformed chunked response: missing chunk terminator");
    }
    position += 2;
  }
}

/// Returns true once the response is complete: a `Content-Length` body has
/// arrived, or a chunked body reached its terminating zero chunk. The gateway
/// keeps the connection open after a response, so waiting for EOF would block
/// until the request timeout.
auto responseComplete(std::string_view response) -> bool {
  const std::size_t header_end = response.find("\r\n\r\n");
  if (header_end == std::string_view::npos) {
    return false;
  }

  bool chunked = false;
  std::optional<std::size_t> content_length;
  const std::string_view header_block = response.substr(0, header_end);
  std::size_t line_start = 0;
  bool first_line = true;
  while (line_start <= header_block.size()) {
    const std::size_t line_end = header_block.find("\r\n", line_start);
    const std::string_view line = line_end == std::string_view::npos
                                      ? header_block.substr(line_start)
                                      : header_block.substr(line_start, line_end - line_start);
    if (!first_line && !line.empty()) {
      const std::size_t colon = line.find(':');
      if (colon != std::string_view::npos) {
        const std::string name = lower(trim(line.substr(0, colon)));
        const std::string_view value = trim(line.substr(colon + 1));
        if (name == "transfer-encoding" && lower(value) == "chunked") {
          chunked = true;
        } else if (name == "content-length") {
          try {
            content_length = static_cast<std::size_t>(std::stoull(std::string(value)));
          } catch (const std::exception&) {
            return false;
          }
        }
      }
    }
    first_line = false;
    if (line_end == std::string_view::npos) {
      break;
    }
    line_start = line_end + 2;
  }

  const std::string_view body = response.substr(header_end + 4);
  if (chunked) {
    return decodeChunked(body).ok();
  }
  if (content_length.has_value()) {
    return body.size() >= *content_length;
  }
  return false;
}

} // namespace

auto Post(uint16_t port, std::string_view path, std::string_view body,
          const std::vector<std::pair<std::string, std::string>>& headers,
          std::chrono::milliseconds timeout) -> absl::StatusOr<HttpResponse> {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return absl::UnavailableError(
        absl::StrCat("failed to create a socket: ", std::strerror(errno)));
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);

  const auto seconds = static_cast<long>(std::max<std::int64_t>(timeout.count(), 1) / 1000);
  const auto micros = static_cast<long>((std::max<std::int64_t>(timeout.count(), 1) % 1000) * 1000);
  timeval time_limit{.tv_sec = seconds, .tv_usec = micros};
  (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &time_limit, sizeof(time_limit));
  (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &time_limit, sizeof(time_limit));

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    const int error = errno;
    ::close(fd);
    return absl::UnavailableError(
        absl::StrCat("failed to connect to 127.0.0.1:", port, ": ", std::strerror(error)));
  }

  std::string request =
      absl::StrCat("POST ", path, " HTTP/1.1\r\nHost: 127.0.0.1:", port,
                   "\r\nContent-Length: ", body.size(), "\r\nConnection: close\r\n");
  for (const auto& [name, value] : headers) {
    absl::StrAppend(&request, name, ": ", value, "\r\n");
  }
  absl::StrAppend(&request, "\r\n", body);

  std::size_t sent = 0;
  while (sent < request.size()) {
    const ssize_t written = ::send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
    if (written <= 0) {
      const int error = errno;
      ::close(fd);
      return absl::UnavailableError(
          absl::StrCat("failed to write request to ", port, ": ", std::strerror(error)));
    }
    sent += static_cast<std::size_t>(written);
  }

  std::string response;
  std::vector<char> buffer(kReadChunkSize);
  for (;;) {
    const ssize_t received = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (received == 0) {
      break;
    }
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ::close(fd);
        return absl::DeadlineExceededError(
            absl::StrCat("timed out waiting for a response from port ", port));
      }
      const int error = errno;
      ::close(fd);
      return absl::UnavailableError(
          absl::StrCat("failed to read response from ", port, ": ", std::strerror(error)));
    }
    response.append(buffer.data(), static_cast<std::size_t>(received));
    if (responseComplete(response)) {
      break;
    }
  }
  ::close(fd);

  const std::size_t header_end = response.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return absl::InternalError(
        absl::StrCat("response from port ", port, " has no header terminator"));
  }

  const std::string_view header_block(response.data(), header_end);
  const std::string_view encoded_body(response.data() + header_end + 4,
                                      response.size() - header_end - 4);

  HttpResponse result;
  bool chunked = false;
  std::optional<std::size_t> content_length;

  std::size_t line_start = 0;
  bool first_line = true;
  while (line_start <= header_block.size()) {
    const std::size_t line_end = header_block.find("\r\n", line_start);
    const std::string_view line = line_end == std::string_view::npos
                                      ? header_block.substr(line_start)
                                      : header_block.substr(line_start, line_end - line_start);

    if (first_line) {
      // "HTTP/1.1 200 OK"
      const std::size_t first_space = line.find(' ');
      if (first_space == std::string_view::npos) {
        return absl::InternalError(
            absl::StrCat("response from port ", port, " has an invalid status line"));
      }
      const std::size_t second_space = line.find(' ', first_space + 1);
      const std::string_view code_text = line.substr(
          first_space + 1, second_space == std::string_view::npos ? std::string_view::npos
                                                                  : second_space - first_space - 1);
      try {
        result.status_code = std::stoi(std::string(code_text));
      } catch (const std::exception&) {
        return absl::InternalError(
            absl::StrCat("response from port ", port, " has an invalid status code"));
      }
      first_line = false;
    } else if (!line.empty()) {
      const std::size_t colon = line.find(':');
      if (colon != std::string_view::npos) {
        const std::string name = lower(trim(line.substr(0, colon)));
        const std::string_view value = trim(line.substr(colon + 1));
        if (name == "transfer-encoding" && lower(value) == "chunked") {
          chunked = true;
        } else if (name == "content-length") {
          try {
            content_length = static_cast<std::size_t>(std::stoull(std::string(value)));
          } catch (const std::exception&) {
            return absl::InternalError(
                absl::StrCat("response from port ", port, " has an invalid Content-Length"));
          }
        }
      }
    }

    if (line_end == std::string_view::npos) {
      break;
    }
    line_start = line_end + 2;
  }

  if (chunked) {
    auto decoded = decodeChunked(encoded_body);
    if (!decoded.ok()) {
      return decoded.status();
    }
    result.body = std::move(*decoded);
  } else if (content_length.has_value() && *content_length < encoded_body.size()) {
    result.body = std::string(encoded_body.substr(0, *content_length));
  } else {
    result.body = std::string(encoded_body);
  }

  return result;
}

} // namespace strij::e2e
