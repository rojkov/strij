#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "gateway/core/node_directory.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/task/task.pb.h"
#include "common/extensions/scheduler.hh"
#include "google/protobuf/message.h"

namespace strij::gateway::schedulers {

// Preserves the pre-existing round-robin behavior: selects available (connected
// and advertising the required protocol) nodes in rotation, advancing the
// selection index on each call. Not requirements-aware. Submits the task to the
// chosen node's connection and resolves the receiver with an error when no node
// is available.
class RoundRobinScheduler final : public extensions::Scheduler {
public:
  RoundRobinScheduler(gateway::NodeDirectory& directory, gateway::ResultReceiverStorage& storage)
      : directory_{directory}, storage_{storage} {}
  ~RoundRobinScheduler() override = default;

  RoundRobinScheduler(const RoundRobinScheduler&) = delete;
  auto operator=(const RoundRobinScheduler&) -> RoundRobinScheduler& = delete;
  RoundRobinScheduler(RoundRobinScheduler&&) noexcept = delete;
  auto operator=(RoundRobinScheduler&&) noexcept -> RoundRobinScheduler& = delete;

  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;

private:
  auto choose(gateway::NodeDirectory& dir) -> gateway::Node*;

  gateway::NodeDirectory& directory_;
  gateway::ResultReceiverStorage& storage_;
  size_t next_index_{0};
};

class RoundRobinSchedulerFactory final : public GatewaySchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, extensions::GatewayFactoryContext& context)
      -> extensions::SchedulerPtr override;
};

} // namespace strij::gateway::schedulers