#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "core/node/capabilities.pb.h"

namespace strij::nodeagent {

// Tracks per-pool in-use and per-task-type in-flight counts derived from
// admissions, completions, and rejections, and enforces admission control on
// each kTaskSubmission. All methods run on the single nodeagent event-loop
// thread; no locking is needed.
//
// Shared free capacity of a pool is `total - sum(reservations) - in_use`
// (handler reservations are excluded from what gateways route on). A task type
// is admitted only while its in-flight count is below the handler's concurrency
// limit; a concurrency of 0 (or an undeclared type) means no limit.
class AdmissionController {
public:
  explicit AdmissionController(const strij::node::NodeCapabilities& capabilities);

  // Attempts to reserve capacity for a task of `task_type` requiring
  // `requirements`. On success the pool and concurrency capacity is reserved;
  // otherwise nothing is reserved and a descriptive error is returned.
  auto Admit(std::string_view task_type,
             const strij::node::ResourceRequirements& requirements) -> absl::Status;

  // Releases capacity previously reserved by Admit(). Each Admit must be paired
  // with exactly one Release (possibly via AdmissionScope).
  void Release(std::string_view task_type,
               const strij::node::ResourceRequirements& requirements);

  // Builds the current kNodeState snapshot. `seq` is caller-owned (monotonic).
  auto BuildStateSnapshot(std::string node_id, uint64_t seq) const
      -> strij::node::NodeState;

  // Shared free capacity of a pool; 0 for undeclared pools.
  auto SharedFree(std::string_view pool) const -> uint64_t;
  // Current in-flight count of a task type.
  auto InFlight(std::string_view task_type) const -> uint64_t;

private:
  // std::less<> enables heterogeneous (string_view) lookups.
  std::map<std::string, uint64_t, std::less<>> pool_total_;
  std::map<std::string, uint64_t, std::less<>> pool_reserved_;
  std::map<std::string, uint64_t, std::less<>> pool_in_use_;
  // 0 means no concurrency limit for the type.
  std::map<std::string, uint64_t, std::less<>> type_concurrency_;
  std::map<std::string, uint64_t, std::less<>> type_in_flight_;
};

// RAII handle releasing admitted capacity when the owning result sender is
// destroyed (a task that never reports a final result still releases its
// reservation) and on the final result. Release() is idempotent. Owns a
// shared_ptr to the controller so capacity can be released even if the scope
// (retained by a long-lived task handler) outlives the call site.
class AdmissionScope {
public:
  AdmissionScope(std::shared_ptr<AdmissionController> controller, std::string task_type,
                 strij::node::ResourceRequirements requirements);
  ~AdmissionScope();

  AdmissionScope(const AdmissionScope&) = delete;
  auto operator=(const AdmissionScope&) -> AdmissionScope& = delete;
  AdmissionScope(AdmissionScope&&) noexcept = delete;
  auto operator=(AdmissionScope&&) noexcept -> AdmissionScope& = delete;

  void Release();

private:
  std::shared_ptr<AdmissionController> controller_;
  std::string task_type_;
  strij::node::ResourceRequirements requirements_;
  bool released_{false};
};

} // namespace strij::nodeagent
