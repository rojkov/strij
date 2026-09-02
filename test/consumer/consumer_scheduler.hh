#pragma once

#include <string>
#include <string_view>

#include "common/task/task.pb.h"
#include "google/protobuf/message.h"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij_consumer {

inline constexpr std::string_view kConsumerSchedulerName = "consumer_scheduler";

// A third-party gateway scheduler exercising the public extension surface from
// an external module: implemented purely against include/strij/ contracts plus
// api/ schemas (no src/ includes, no src/ vision into registry internals).
class ConsumerScheduler final : public strij::extensions::Scheduler {
public:
  ConsumerScheduler() = default;
  ~ConsumerScheduler() override = default;

  ConsumerScheduler(const ConsumerScheduler&) = delete;
  auto operator=(const ConsumerScheduler&) -> ConsumerScheduler& = delete;
  ConsumerScheduler(ConsumerScheduler&&) noexcept = delete;
  auto operator=(ConsumerScheduler&&) noexcept -> ConsumerScheduler& = delete;

  // Smoke behavior: reject every task with a structured error through its
  // receiver (the contract requires every schedule to resolve).
  void Schedule(const strij::task::Task& /*task*/,
                strij::gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
};

class ConsumerSchedulerFactory final : public strij::gateway::GatewaySchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config,
              strij::extensions::GatewayFactoryContext& context)
      -> strij::extensions::SchedulerPtr override;
};

} // namespace strij_consumer