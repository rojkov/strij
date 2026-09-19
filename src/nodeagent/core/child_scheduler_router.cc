#include "nodeagent/core/child_scheduler_router.hh"

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
#include "common/config/extensions.pb.h"
#include "common/extensions/scheduler_loader.hh"
#include "common/task/task.pb.h"
#include "nodeagent/config/nodeagent.pb.h"
#include "strij/extensions/factory_context.hh"
#include "strij/extensions/scheduler.hh"

namespace strij::nodeagent {

namespace {

auto in_types(std::span<const uint8_t> types, uint8_t type_id) -> bool {
  return std::ranges::find(types, type_id) != types.end();
}

} // namespace

ChildSchedulerRouter::ChildSchedulerRouter(std::vector<ChildRoutedScheduler> schedulers)
    : schedulers_{std::move(schedulers)} {
  std::set<uint8_t> seen_types;
  for (const auto& routed : schedulers_) {
    if (required_protocol_.empty() && !routed.scheduler->RequiredProtocol().empty()) {
      required_protocol_ = routed.scheduler->RequiredProtocol();
    }

    for (const uint8_t type_id : routed.scheduler->HandledFrameTypes()) {
      if (seen_types.insert(type_id).second) {
        handled_types_.push_back(type_id);
      }
    }
  }
}

void ChildSchedulerRouter::Submit(task::Task task, gateway::ResultReceiverPtr receiver) {
  Schedule(task, std::move(receiver));
}

void ChildSchedulerRouter::Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) {
  extensions::Scheduler* scheduler = findChildScheduler(task);
  if (scheduler == nullptr) {
    receiver->DeliverError(absl::StrCat("no local scheduler claims task type '", task.type(),
                                        "' and no local default is declared"));
    return;
  }

  scheduler->Schedule(task, std::move(receiver));
}

auto ChildSchedulerRouter::findChildScheduler(const task::Task& task) -> extensions::Scheduler* {
  extensions::Scheduler* fallback = nullptr;
  for (auto& routed : schedulers_) {
    if (routed.local_default) {
      fallback = routed.scheduler.get();
    } else if (!routed.task_type.empty() && routed.task_type == task.type()) {
      return routed.scheduler.get();
    }
  }

  return fallback;
}

auto ChildSchedulerRouter::findFrameOwner(uint8_t type_id) -> extensions::Scheduler* {
  for (auto& routed : schedulers_) {
    if (in_types(routed.scheduler->HandledFrameTypes(), type_id)) {
      return routed.scheduler.get();
    }
  }

  return nullptr;
}

auto ChildSchedulerRouter::RequiredProtocol() const -> std::string_view { return required_protocol_; }

auto ChildSchedulerRouter::HandledFrameTypes() const -> std::span<const uint8_t> {
  return handled_types_;
}

auto ChildSchedulerRouter::HandleFrame(const io::TlvFrame& frame, io::Connection& conn)
    -> absl::Status {
  extensions::Scheduler* owner = findFrameOwner(frame.type_id);
  if (owner == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("no constituent scheduler owns frame type ", static_cast<int>(frame.type_id)));
  }

  return owner->HandleFrame(frame, conn);
}

auto BuildChildSchedulerRouter(const config::NodeAgentConfig& config,
                               extensions::NodeagentFactoryContext& context)
    -> absl::StatusOr<std::unique_ptr<ChildSchedulerRouter>> {
  if (config.schedulers().empty()) {
    return absl::InvalidArgumentError(
        "NodeAgentConfig.schedulers is empty: at least one scheduler must be configured");
  }

  std::vector<ChildSchedulerRouter::ChildRoutedScheduler> routed;
  routed.reserve(static_cast<size_t>(config.schedulers().size()));
  // Validate the whole role table before touching the factory context: an
  // invalid config must fail without creating any scheduler (fail fast, no
  // side effects on the context).
  std::set<std::string> claimed_types;
  size_t local_default_count = 0;
  for (const auto& scheduler_config : config.schedulers()) {
    // Node-side authority doctrine: an empty task_type is NOT a default — only
    // an explicit local_default=true marks the fallback authority.
    if (!scheduler_config.task_type().empty() &&
        !claimed_types.insert(scheduler_config.task_type()).second) {
      return absl::InvalidArgumentError(absl::StrCat(
          "NodeAgentConfig.schedulers claims task_type '", scheduler_config.task_type(),
          "' more than once"));
    }

    if (scheduler_config.local_default() && ++local_default_count > 1) {
      return absl::InvalidArgumentError(
          "NodeAgentConfig.schedulers has more than one local_default entry");
    }
  }

  for (const auto& scheduler_config : config.schedulers()) {
    auto scheduler_result = CreateNodeScheduler(scheduler_config.extension(), context);
    if (!scheduler_result.ok()) {
      return scheduler_result.status();
    }

    routed.push_back(ChildSchedulerRouter::ChildRoutedScheduler{
        .scheduler = std::move(scheduler_result).value(),
        .task_type = scheduler_config.task_type(),
        .local_default = scheduler_config.local_default()});
  }

  return std::make_unique<ChildSchedulerRouter>(std::move(routed));
}

} // namespace strij::nodeagent