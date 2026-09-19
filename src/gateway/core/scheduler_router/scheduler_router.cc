#include "gateway/core/scheduler_router/scheduler_router.hh"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
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
#include "gateway/core/node.hh"
#include "gateway/core/node_connection_result_receiver.hh"
#include "strij/extensions/factory_context.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij::gateway {

namespace {

auto in_types(std::span<const uint8_t> types, uint8_t type_id) -> bool {
  return std::ranges::find(types, type_id) != types.end();
}

// Forwards outcome calls to the receiver currently registered in
// ResultReceiverStorage under `task_id`, erasing the entry on terminal
// outcomes. Handed to the per-type scheduler's Schedule when routing an
// upstream child submission: the scheduler either accepts the task (storing it
// itself is a no-op — the node-connection receiver was already registered by
// the router) or delivers an error through this forwarding receiver, which
// surfaces the error on the submitting node's connection and erases the entry.
class StoredReceiverForwarder final : public gateway::ResultReceiver {
public:
  StoredReceiverForwarder(ResultReceiverStorage& storage, std::string task_id)
      : storage_{&storage}, task_id_{std::move(task_id)} {}

  void Deliver(std::span<const std::byte> value, bool is_final) override {
    gateway::ResultReceiver* receiver = storage_->Get(task_id_);
    if (receiver == nullptr) {
      return;
    }
    receiver->Deliver(value, is_final);
    if (is_final) {
      storage_->Erase(task_id_);
    }
  }

  void DeliverError(std::string_view reason) override {
    gateway::ResultReceiver* receiver = storage_->Get(task_id_);
    if (receiver != nullptr) {
      receiver->DeliverError(reason);
    }
    storage_->Erase(task_id_);
  }

private:
  ResultReceiverStorage* storage_;
  std::string task_id_;
};

} // namespace

SchedulerRouter::SchedulerRouter(std::vector<RoutedScheduler> schedulers,
                                 gateway::ResultReceiverStorage& storage)
    : schedulers_{std::move(schedulers)}, storage_{storage} {
  // The router owns the upstream child-forward claim (kTaskSubmission) itself.
  handled_types_.push_back(io::TlvFrame::kTaskSubmission);
  std::set<uint8_t> seen_types{io::TlvFrame::kTaskSubmission};
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

auto SchedulerRouter::HandledFrameTypes() const -> std::span<const uint8_t> {
  return handled_types_;
}

auto SchedulerRouter::handleChildSubmission(const io::TlvFrame& frame, io::Connection& conn)
    -> absl::Status {
  task::Task task;
  if (!task.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                           static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed task::Task in kTaskSubmission frame");
  }

  Node* node = dynamic_cast<Node*>(conn.GetOwner());
  if (node == nullptr) {
    return absl::InvalidArgumentError(
        "kTaskSubmission on a connection without an owning gateway Node");
  }

  // Register the node-connection receiver keyed by the SUBMITTING node so the
  // existing disconnect cleanup unwinds a node's outstanding forwarded children.
  storage_.Put(task.id(), std::make_unique<NodeConnectionResultReceiver>(task.id(), conn.Mailbox()),
               node->GetNodeId());

  // Route through the normal per-type dispatch. The constituent scheduler's own
  // storage registration is a no-op (the entry already exists); its error path
  // surfaces through the forwarding receiver and erases the node-connection
  // entry. An unmatched type with no default likewise errors and erases.
  extensions::Scheduler* scheduler = findSchedulerFor(task);
  if (scheduler == nullptr) {
    if (gateway::ResultReceiver* receiver = storage_.Get(task.id()); receiver != nullptr) {
      receiver->DeliverError(
          absl::StrCat("no scheduler configured for task type '", task.type(), "'"));
    }
    storage_.Erase(task.id());
    return absl::OkStatus();
  }

  scheduler->Schedule(task, std::make_unique<StoredReceiverForwarder>(storage_, task.id()));
  return absl::OkStatus();
}

auto SchedulerRouter::HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status {
  // kTaskSubmission is a router-owned frame type: the node itself silently
  // forwards a child upstream, so the router (not a wire-protocol scheduler)
  // owns the claim. Everything else routes to the constituent that owns it.
  if (frame.type_id == io::TlvFrame::kTaskSubmission) {
    return handleChildSubmission(frame, conn);
  }

  extensions::Scheduler* owner = findFrameOwner(frame.type_id);
  if (owner == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("no constituent scheduler owns frame type ", static_cast<int>(frame.type_id)));
  }

  return owner->HandleFrame(frame, conn);
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

  return std::make_unique<SchedulerRouter>(std::move(routed), context.ResultReceiverStorage());
}

} // namespace strij::gateway