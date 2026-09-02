#include "gateway/core/scheduler_router/scheduler_router.hh"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/extensions/scheduler_loader.hh"
#include "common/task/task.pb.h"
#include "gateway/config/gateway.pb.h"
#include "strij/extensions/factory_context.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij::gateway {

namespace {

auto in_types(std::span<const uint8_t> types, uint8_t type_id) -> bool {
  return std::ranges::find(types, type_id) != types.end();
}

} // namespace

SchedulerRouter::SchedulerRouter(std::vector<RoutedScheduler> schedulers)
    : schedulers_{std::move(schedulers)} {
  std::set<uint8_t> seen_types;
  for (const auto& routed : schedulers_) {
    if (required_protocol_.empty()) {
      required_protocol_ = routed.scheduler->RequiredProtocol();
    }

    for (const uint8_t type_id : routed.scheduler->HandledFrameTypes()) {
      if (seen_types.insert(type_id).second) {
        handled_types_.push_back(type_id);
      }
    }
  }
}

auto SchedulerRouter::findSchedulerFor(const task::Task& task) -> extensions::Scheduler* {
  extensions::Scheduler* fallback = nullptr;
  for (auto& routed : schedulers_) {
    if (routed.task_type.empty()) {
      fallback = routed.scheduler.get();
    } else if (routed.task_type == task.type()) {
      return routed.scheduler.get();
    }
  }

  return fallback;
}

auto SchedulerRouter::findFrameOwner(uint8_t type_id) -> extensions::Scheduler* {
  for (auto& routed : schedulers_) {
    if (in_types(routed.scheduler->HandledFrameTypes(), type_id)) {
      return routed.scheduler.get();
    }
  }

  return nullptr;
}

void SchedulerRouter::Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) {
  extensions::Scheduler* scheduler = findSchedulerFor(task);
  if (scheduler == nullptr) {
    receiver->DeliverError(
        absl::StrCat("no scheduler configured for task type '", task.type(), "'"));

    return;
  }

  scheduler->Schedule(task, std::move(receiver));
}

auto SchedulerRouter::RequiredProtocol() const -> std::string_view { return required_protocol_; }

auto SchedulerRouter::HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status {
  extensions::Scheduler* owner = findFrameOwner(frame.type_id);
  if (owner == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("no constituent scheduler owns frame type ", static_cast<int>(frame.type_id)));
  }

  return owner->HandleFrame(frame, conn);
}

auto SchedulerRouter::HandledFrameTypes() const -> std::span<const uint8_t> {
  return handled_types_;
}

auto BuildSchedulerRouter(const config::GatewayConfig& config,
                          extensions::GatewayFactoryContext& context)
    -> absl::StatusOr<std::unique_ptr<SchedulerRouter>> {
  if (config.schedulers().empty()) {
    return absl::InvalidArgumentError(
        "GatewayConfig.schedulers is empty: at least one scheduler must be configured");
  }

  std::vector<SchedulerRouter::RoutedScheduler> routed;
  routed.reserve(static_cast<size_t>(config.schedulers().size()));
  std::set<std::string> bound_types;
  bool has_default = false;
  for (const auto& scheduler_config : config.schedulers()) {
    if (scheduler_config.task_type().empty()) {
      if (has_default) {
        return absl::InvalidArgumentError(
            "GatewayConfig.schedulers has more than one default scheduler (entry with empty "
            "task_type)");
      }

      has_default = true;
    } else if (!bound_types.insert(scheduler_config.task_type()).second) {
      return absl::InvalidArgumentError(absl::StrCat("GatewayConfig.schedulers binds task_type '",
                                                     scheduler_config.task_type(),
                                                     "' more than once"));
    }

    auto scheduler_result = CreateGatewayScheduler(scheduler_config.extension(), context);
    if (!scheduler_result.ok()) {
      return scheduler_result.status();
    }

    routed.push_back(
        SchedulerRouter::RoutedScheduler{.scheduler = std::move(scheduler_result).value(),
                                         .task_type = scheduler_config.task_type()});
  }

  return std::make_unique<SchedulerRouter>(std::move(routed));
}

} // namespace strij::gateway