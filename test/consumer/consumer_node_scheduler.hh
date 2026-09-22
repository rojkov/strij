#pragma once

#include <string>
#include <string_view>

#include "strij/extensions/scheduler.hh"

namespace strij_consumer {

inline constexpr std::string_view kConsumerNodeSchedulerName = "consumer_node_scheduler";

// A third-party nodeagent-side scheduler extension, composed into a nodeagent
// binary via strij_nodeagent_binary. Like the gateway-side ConsumerScheduler it
// is implemented purely against @strij's public contracts; it deliberately
// leaves the TLV-frame facet (HandleFrame/HandledFrameTypes) at the default
// no-op so it needs no io::TlvFrame/io::Connection reachability.
class ConsumerNodeScheduler final : public strij::extensions::Scheduler {
public:
  ConsumerNodeScheduler() = default;
  ~ConsumerNodeScheduler() override = default;

  ConsumerNodeScheduler(const ConsumerNodeScheduler&) = delete;
  auto operator=(const ConsumerNodeScheduler&) -> ConsumerNodeScheduler& = delete;
  ConsumerNodeScheduler(ConsumerNodeScheduler&&) noexcept = delete;
  auto operator=(ConsumerNodeScheduler&&) noexcept -> ConsumerNodeScheduler& = delete;

  void Schedule(const strij::task::Task& task, strij::gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
};

class ConsumerNodeSchedulerFactory final : public strij::nodeagent::NodeSchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config,
              const strij::nodeagent::NodeSchedulerDeps& deps)
      -> strij::extensions::SchedulerPtr override;
};

} // namespace strij_consumer