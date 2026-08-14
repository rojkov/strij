#pragma once

#include <string>
#include <string_view>

#include "google/protobuf/message.h"

#include "extensions/schedulers/scheduler.hh"

namespace strij::extensions::schedulers {

// Excludes nodes whose shared-free pool capacity is below the offer's
// ResourceRequirements or whose per-type concurrency is exhausted, then picks
// the least-loaded eligible node (lowest used-concurrency ratio, then lowest
// node-wide in-flight count).
class CapabilityAwareScheduler final : public Scheduler {
public:
  auto RequiredProtocol() const -> std::string_view override;
  auto Choose(strij::gateway::NodeDirectory& dir, const TaskOffer& offer)
      -> strij::gateway::Node* override;
};

class CapabilityAwareSchedulerFactory final : public SchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<Scheduler> override;
};

} // namespace strij::extensions::schedulers
