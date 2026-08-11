#include "core/nodeagent/admission_controller.hh"

#include <chrono>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "core/logging/log.hh"
#include "core/node/capabilities.pb.h"

namespace strij::nodeagent {

AdmissionController::AdmissionController(const strij::node::NodeCapabilities& capabilities) {
  for (const auto& pool : capabilities.pools()) {
    pool_total_[pool.name()] = pool.total();
  }
  for (const auto& reservation : capabilities.reservations()) {
    pool_reserved_[reservation.pool()] += reservation.amount();
  }
  for (const auto& handler : capabilities.handlers()) {
    type_concurrency_[handler.task_type()] = handler.concurrency();
  }
}

auto AdmissionController::Admit(std::string_view task_type,
                                const strij::node::ResourceRequirements& requirements)
    -> absl::Status {
  for (const auto& [pool, amount] : requirements.resources()) {
    if (!pool_total_.contains(pool)) {
      return absl::FailedPreconditionError(
          absl::StrCat("Task type '", task_type, "' requests undeclared pool '", pool, "'"));
    }
    const uint64_t shared_free = SharedFree(pool);
    if (amount > shared_free) {
      return absl::ResourceExhaustedError(
          absl::StrCat("Pool '", pool, "' exhausted: shared free ", shared_free,
                       ", required ", amount));
    }
  }

  const auto concurrency_iter = type_concurrency_.find(task_type);
  if (concurrency_iter != type_concurrency_.end() && concurrency_iter->second != 0) {
    const uint64_t in_flight = InFlight(task_type);
    if (in_flight >= concurrency_iter->second) {
      return absl::ResourceExhaustedError(
          absl::StrCat("Task type '", task_type, "' at concurrency limit ",
                       concurrency_iter->second));
    }
  }

  for (const auto& [pool, amount] : requirements.resources()) {
    pool_in_use_[pool] += amount;
  }
  ++type_in_flight_[std::string(task_type)];
  return absl::OkStatus();
}

void AdmissionController::Release(std::string_view task_type,
                                  const strij::node::ResourceRequirements& requirements) {
  for (const auto& [pool, amount] : requirements.resources()) {
    auto iter = pool_in_use_.find(pool);
    if (iter == pool_in_use_.end()) {
      LOG_WARNING("Admission release underflow for pool '{}' (task type '{}')", pool, task_type);
      continue;
    }
    if (amount >= iter->second) {
      pool_in_use_.erase(iter);
    } else {
      iter->second -= amount;
    }
  }

  auto in_flight_iter = type_in_flight_.find(task_type);
  if (in_flight_iter == type_in_flight_.end() || in_flight_iter->second == 0) {
    LOG_WARNING("Admission release underflow for task type '{}'", task_type);
    return;
  }
  if (--in_flight_iter->second == 0) {
    type_in_flight_.erase(in_flight_iter);
  }
}

auto AdmissionController::BuildStateSnapshot(std::string node_id, uint64_t seq) const
    -> strij::node::NodeState {
  strij::node::NodeState state;
  state.set_node_id(std::move(node_id));
  state.set_seq(seq);
  state.set_timestamp(
      static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count()));

  for (const auto& [pool, total] : pool_total_) {
    (void)total;
    auto* usage = state.add_pools();
    usage->set_pool(pool);
    const auto iter = pool_in_use_.find(pool);
    usage->set_in_use(iter == pool_in_use_.end() ? 0 : iter->second);
  }

  uint64_t node_in_flight = 0;
  for (const auto& [type, in_flight] : type_in_flight_) {
    node_in_flight += in_flight;
    auto* usage = state.add_type_usage();
    usage->set_task_type(type);
    usage->set_in_flight(in_flight);
  }
  state.set_in_flight(node_in_flight);

  return state;
}

auto AdmissionController::SharedFree(std::string_view pool) const -> uint64_t {
  const auto total_iter = pool_total_.find(pool);
  if (total_iter == pool_total_.end()) {
    return 0;
  }
  const uint64_t reserved = [&] {
    const auto iter = pool_reserved_.find(pool);
    return iter == pool_reserved_.end() ? 0 : iter->second;
  }();
  const uint64_t in_use = [&] {
    const auto iter = pool_in_use_.find(pool);
    return iter == pool_in_use_.end() ? 0 : iter->second;
  }();
  return total_iter->second > reserved + in_use ? total_iter->second - reserved - in_use : 0;
}

auto AdmissionController::InFlight(std::string_view task_type) const -> uint64_t {
  const auto iter = type_in_flight_.find(task_type);
  return iter == type_in_flight_.end() ? 0 : iter->second;
}

AdmissionScope::AdmissionScope(std::shared_ptr<AdmissionController> controller,
                               std::string task_type,
                               strij::node::ResourceRequirements requirements)
    : controller_{std::move(controller)},
      task_type_{std::move(task_type)},
      requirements_{std::move(requirements)} {}

AdmissionScope::~AdmissionScope() { Release(); }

void AdmissionScope::Release() {
  if (released_) {
    return;
  }
  released_ = true;
  controller_->Release(task_type_, requirements_);
}

} // namespace strij::nodeagent
