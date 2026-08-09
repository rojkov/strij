#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"

namespace strij::gateway {

/**
 * @brief Pure, stateful HTTP response framer.
 *
 * Decides the framing once at the first delivered result from that result's
 * finality:
 *   first-final   -> single Content-Length response (one frame)
 *   first-non-final -> Transfer-Encoding: chunked (status frame + one chunk
 *                      frame per result, terminal 0\r\n\r\n on the final one)
 * Returns zero or more byte frames to write on the connection. Header-declared
 * so the framing logic is testable without a Connection or dispatcher.
 */
class HttpResponseFramer final {
public:
  enum class State : uint8_t { kIdle, kChunked, kDone };

  auto Next(std::span<const std::byte> body, bool is_final) -> std::vector<std::vector<std::byte>>;

private:
  State state_{State::kIdle};
};

class HttpResultReceiver final : public ResultReceiver {
public:
  explicit HttpResultReceiver(strij::io::Connection& conn) : conn_{conn} {}

  void Deliver(std::span<const std::byte> value, bool is_final) override;

private:
  strij::io::Connection& conn_;
  HttpResponseFramer framer_;
};

} // namespace strij::gateway
