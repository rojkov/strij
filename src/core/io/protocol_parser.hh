#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "carrot/common/pure.hh"

namespace carrot::io {

class ProtocolParser {
public:
  enum class Action : uint8_t { NeedMoreData = 0, MessageComplete = 1 };

  ProtocolParser() = default;
  virtual ~ProtocolParser() = default;

  ProtocolParser(const ProtocolParser&) = delete;
  auto operator=(const ProtocolParser&) -> ProtocolParser& = delete;
  ProtocolParser(ProtocolParser&&) noexcept = delete;
  auto operator=(ProtocolParser&&) noexcept -> ProtocolParser& = delete;

  /**
   * @brief Provide a writable buffer for io_uring to read into.
   * The returned span MUST remain valid until the next OnData() call.
   */
  virtual auto GetReadBuffer() -> std::span<std::byte> PURE;

  /**
   * @brief Process bytes_read bytes that were written into GetReadBuffer().
   * Data is in the parser's own buffer — process in-place, no copy needed.
   * @return Action::NeedMoreData if more input is required,
   *         Action::MessageComplete if a complete message was assembled.
   */
  virtual auto OnData(size_t bytes_read) -> Action PURE;
};

using ProtocolParserPtr = std::unique_ptr<ProtocolParser>;

} // namespace carrot::io
