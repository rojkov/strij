#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/core/io/connection.hh"
#include "common/task/task.pb.h"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij::gateway {

/**
 * @brief Pure, stateful HTTP response framer.
 *
 * Decides the framing once at the first delivered result from that result's
 * finality:
 *   first-final   -> single Content-Length response (one frame)
 *   first-non-final -> Transfer-Encoding: chunked (status frame + one chunk
 *                      frame per result, terminal 0\r\n\r\n on the final one)
 * The status line is fixed at that same first result: success writes the
 * status mapped from TASK_STATUS_OK, a failure writes the status mapped from
 * its code, and a failure arriving later produces no frames at all.
 * Returns zero or more byte frames to write on the connection.
 * Header-declared so the framing logic is testable without a Connection or
 * dispatcher.
 */
class HttpResponseFramer final {
public:
  enum class State : uint8_t { kIdle, kChunked, kDone };

  auto Next(std::span<const std::byte> body, bool is_final) -> std::vector<std::vector<std::byte>>;
  // Builds a single HTTP error response for `status` and transitions to kDone
  // so no further frames are emitted. Returns empty frames when the response
  // has already begun — the status line written at the first result is never
  // revised.
  auto ErrorResponse(std::string_view reason, task::TaskStatus status)
      -> std::vector<std::vector<std::byte>>;

private:
  State state_{State::kIdle};
};

class HttpResultReceiver final : public ResultReceiver {
public:
  HttpResultReceiver(strij::io::Connection& conn, std::string_view task_id)
      : conn_{conn}, task_id_{task_id} {}

  void Deliver(std::span<const std::byte> value, bool is_final) override;
  void DeliverError(std::string_view reason, task::TaskStatus status) override;

private:
  strij::io::Connection& conn_;
  HttpResponseFramer framer_;
  // Named in diagnostics: the response itself carries no task id.
  std::string task_id_;
};

} // namespace strij::gateway
