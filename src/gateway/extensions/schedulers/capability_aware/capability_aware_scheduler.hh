#pragma once

#include <string>
#include <string_view>

#include "gateway/core/node_directory.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/task/task.pb.h"
#include "strij/extensions/scheduler.hh"
#include "google/protobuf/message.h"

namespace strij::gateway::schedulers {

// Excludes nodes whose shared-free pool capacity is below the task's
// ResourceRequirements or whose per-type concurrency is exhausted, then picks
// the least-loaded eligible node (lowest used-concurrency ratio, then lowest
// node-wide in-flight count). Submits the task to the chosen node's connection
// and resolves the receiver with an error when no node is eligible.
class CapabilityAwareScheduler final : public extensions::Scheduler {
public:
  CapabilityAwareScheduler(gateway::NodeDirectory& directory,
                           gateway::ResultReceiverStorage& storage)
      : directory_{directory}, storage_{storage} {}
  ~CapabilityAwareScheduler() override = default;

  CapabilityAwareScheduler(const CapabilityAwareScheduler&) = delete;
  auto operator=(const CapabilityAwareScheduler&) -> CapabilityAwareScheduler& = delete;
  CapabilityAwareScheduler(CapabilityAwareScheduler&&) noexcept = delete;
  auto operator=(CapabilityAwareScheduler&&) noexcept -> CapabilityAwareScheduler& = delete;

  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;

private:
  auto choose(gateway::NodeDirectory& dir, const task::Task& task) const -> gateway::Node*;

  gateway::NodeDirectory& directory_;
  gateway::ResultReceiverStorage& storage_;
};

class CapabilityAwareSchedulerFactory final : public GatewaySchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, extensions::GatewayFactoryContext& context)
      -> extensions::SchedulerPtr override;
};

} // namespace strij::gateway::schedulers