#pragma once

#include "strij/common/pure.hh"

namespace strij::io {
struct TlvFrame;
} // namespace strij::io

namespace strij::nodeagent {

// The inbound side of the node event client. The core TLV handler depends on
// this narrow seam rather than the concrete client or the handler-facing
// EventBus contract.
class EventDeliverySink {
public:
  EventDeliverySink() = default;
  virtual ~EventDeliverySink() = default;

  EventDeliverySink(const EventDeliverySink&) = delete;
  auto operator=(const EventDeliverySink&) -> EventDeliverySink& = delete;
  EventDeliverySink(EventDeliverySink&&) noexcept = delete;
  auto operator=(EventDeliverySink&&) noexcept -> EventDeliverySink& = delete;

  virtual void HandleDelivery(const io::TlvFrame& frame) PURE;
  // Dispatches an inbound kEventSubscribeResult frame.
  virtual void HandleSubscribeResult(const io::TlvFrame& frame) PURE;
};

} // namespace strij::nodeagent
