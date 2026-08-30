#include "gateway/extensions/schedulers/capability_aware/capability_aware_scheduler.hh"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "gateway/core/node.hh"
#include "gateway/core/node_directory.hh"
#include "gateway/extensions/schedulers/capability_aware/capability_aware.pb.h"
#include "strij/extensions/extension_registry.hh"
#include "strij/extensions/factory_context.hh"
#include "strij/extensions/scheduler.hh"

namespace strij::gateway::schedulers {

namespace {

auto findHandler(const node::NodeCapabilities* caps, std::string_view task_type)
    -> const node::HandlerCapability* {
  if (caps == nullptr) {
    return nullptr;
  }

  for (const auto& handler : caps->handlers()) {
    if (handler.task_type() == task_type) {
      return &handler;
    }
  }

  return nullptr;
}

auto poolTotal(const node::NodeCapabilities* caps, std::string_view pool) -> uint64_t {
  if (caps == nullptr) {
    return 0;
  }

  for (const auto& declared : caps->pools()) {
    if (declared.name() == pool) {
      return declared.total();
    }
  }

  return 0;
}

// Capacity pinned by reservations, excluded from the shared pool gateways route
// on.
auto poolReserved(const node::NodeCapabilities* caps, std::string_view pool) -> uint64_t {
  if (caps == nullptr) {
    return 0;
  }

  uint64_t reserved = 0;
  for (const auto& reservation : caps->reservations()) {
    if (reservation.pool() == pool) {
      reserved += reservation.amount();
    }
  }

  return reserved;
}

auto poolInUse(const node::NodeState* state, std::string_view pool) -> uint64_t {
  if (state == nullptr) {
    return 0;
  }

  for (const auto& usage : state->pools()) {
    if (usage.pool() == pool) {
      return usage.in_use();
    }
  }

  return 0;
}

auto typeInFlight(const node::NodeState* state, std::string_view task_type) -> uint64_t {
  if (state == nullptr) {
    return 0;
  }

  for (const auto& usage : state->type_usage()) {
    if (usage.task_type() == task_type) {
      return usage.in_flight();
    }
  }

  return 0;
}

auto nodeInFlight(const node::NodeState* state) -> uint64_t {
  return state == nullptr ? 0 : state->in_flight();
}

// Whether the node can currently take the task: it declares a handler for the
// task type (or declares no handlers at all), has per-type concurrency
// headroom, and every required pool has enough shared-free capacity.
auto eligible(const gateway::Node* node, const task::Task& task) -> bool {
  const auto* caps = node->GetCapabilities();
  // A node without an advertisement cannot be verified; exclude it until the
  // advertisement arrives.
  if (caps == nullptr) {
    return false;
  }

  const auto* state = node->GetState();

  const auto& task_type = task.type();
  const auto* handler = findHandler(caps, task_type);

  if (caps->handlers_size() > 0 && handler == nullptr) {
    // The node declares handlers but none for this type.
    return false;
  }

  if (handler != nullptr && handler->concurrency() > 0 &&
      typeInFlight(state, task_type) >= handler->concurrency()) {
    return false;
  }

  return std::ranges::all_of(task.requirements().resources(), [&](const auto& resource) -> bool {
    const auto& [pool, amount] = resource;
    const uint64_t total = poolTotal(caps, pool);
    const uint64_t reserved = poolReserved(caps, pool);
    const uint64_t shared_capacity = total > reserved ? total - reserved : 0;
    const uint64_t in_use = poolInUse(state, pool);
    const uint64_t shared_free = shared_capacity > in_use ? shared_capacity - in_use : 0;

    return shared_free >= amount;
  });
}

// Load metric: fraction of the per-type concurrency limit in use when a limit
// is declared, otherwise neutral (1.0). Lower is less loaded.
auto loadRatio(const gateway::Node* node, const task::Task& task) -> double {
  const auto* state = node->GetState();
  const auto* handler = findHandler(node->GetCapabilities(), task.type());
  if (handler != nullptr && handler->concurrency() > 0) {
    const uint64_t concurrency = handler->concurrency();
    const uint64_t used = std::min(typeInFlight(state, task.type()), concurrency);

    return static_cast<double>(used) / static_cast<double>(concurrency);
  }

  return 1.0;
}

} // namespace

auto CapabilityAwareScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

auto CapabilityAwareScheduler::choose(gateway::NodeDirectory& dir, const task::Task& task) const
    -> gateway::Node* {
  gateway::Node* best = nullptr;
  double best_ratio = 0.0;
  uint64_t best_in_flight = 0;

  for (gateway::Node* node : dir.GetCandidates(RequiredProtocol())) {
    if (!eligible(node, task)) {
      continue;
    }

    const double ratio = loadRatio(node, task);
    const uint64_t in_flight = nodeInFlight(node->GetState());

    if (best == nullptr || ratio < best_ratio ||
        (ratio == best_ratio && in_flight < best_in_flight)) {
      best = node;
      best_ratio = ratio;
      best_in_flight = in_flight;
    }
  }

  return best;
}

void CapabilityAwareScheduler::Schedule(const task::Task& task,
                                        gateway::ResultReceiverPtr receiver) {
  gateway::Node* node = choose(directory_, task);
  if (node == nullptr) {
    receiver->DeliverError("no eligible node for task submission");
    return;
  }

  std::string serialized;
  if (!task.SerializeToString(&serialized)) {
    receiver->DeliverError("failed to serialize task for submission");
    return;
  }

  storage_.Put(task.id(), std::move(receiver), std::string(node->GetNodeId()));

  auto frame =
      io::SerializeTlvFrame(io::TlvFrame::kTaskSubmission,
                            std::as_bytes(std::span(serialized.data(), serialized.size())));
  node->GetConnection()->Write(frame);
}

auto CapabilityAwareSchedulerFactory::Name() const -> std::string { return "capability_aware"; }

auto CapabilityAwareSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<
      extensions::schedulers::capability_aware::CapabilityAwareSchedulerConfig>();
}

auto CapabilityAwareSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                             extensions::GatewayFactoryContext& context)
    -> extensions::SchedulerPtr {
  return std::make_unique<CapabilityAwareScheduler>(context.NodeDirectory(),
                                                    context.ResultReceiverStorage());
}

} // namespace strij::gateway::schedulers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::gateway::schedulers::CapabilityAwareSchedulerFactory,
                                 strij::gateway::GatewaySchedulerFactory,
                                 capability_aware_scheduler_registrar)