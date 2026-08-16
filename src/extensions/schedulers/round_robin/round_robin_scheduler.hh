#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "extensions/schedulers/scheduler.hh"
#include "google/protobuf/message.h"

namespace strij::extensions::schedulers {

// Preserves the pre-existing round-robin behavior: selects available (connected
// and advertising the required protocol) nodes in rotation, advancing the
// selection index on each call. Not requirements-aware.
class RoundRobinScheduler final : public Scheduler {
public:
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
  auto Choose(gateway::NodeDirectory& dir, const TaskOffer& offer) -> gateway::Node* override;

private:
  size_t next_index_{0};
};

class RoundRobinSchedulerFactory final : public SchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> SchedulerPtr override;
};

} // namespace strij::extensions::schedulers
