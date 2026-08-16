#pragma once

#include <string>
#include <string_view>

#include "extensions/schedulers/scheduler.hh"
#include "google/protobuf/message.h"

namespace strij::extensions::schedulers {

// Excludes nodes whose shared-free pool capacity is below the offer's
// ResourceRequirements or whose per-type concurrency is exhausted, then picks
// the least-loaded eligible node (lowest used-concurrency ratio, then lowest
// node-wide in-flight count).
class CapabilityAwareScheduler final : public Scheduler {
public:
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
  auto Choose(gateway::NodeDirectory& dir, const TaskOffer& offer) -> gateway::Node* override;
};

class CapabilityAwareSchedulerFactory final : public SchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> SchedulerPtr override;
};

} // namespace strij::extensions::schedulers
