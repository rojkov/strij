#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "common/node/capabilities.pb.h"
#include "strij/common/pure.hh"
#include "strij/event/command_handler.hh"

namespace strij::nodeagent {

// Pure-abstract contract for the nodeagent's admission controller, reached by
// the run-task service and node schedulers via NodeSchedulerDeps::admission_.
// Extension authors consume it through this interface rather than the concrete
// AdmissionControllerImpl and SHALL NOT subclass the Impl.
class AdmissionController {
public:
  AdmissionController() = default;
  virtual ~AdmissionController() = default;

  AdmissionController(const AdmissionController&) = delete;
  auto operator=(const AdmissionController&) -> AdmissionController& = delete;
  AdmissionController(AdmissionController&&) noexcept = delete;
  auto operator=(AdmissionController&&) noexcept -> AdmissionController& = delete;

  // Attempts to reserve capacity for a task of `task_type` requiring
  // `requirements`. On success the pool and concurrency capacity is reserved;
  // otherwise nothing is reserved and a descriptive error is returned.
  virtual auto Admit(std::string_view task_type, const node::ResourceRequirements& requirements)
      -> absl::Status PURE;

  // Releases capacity previously reserved by Admit(). Each Admit must be paired
  // with exactly one Release (possibly via AdmissionScope).
  virtual void Release(std::string_view task_type,
                       const node::ResourceRequirements& requirements) PURE;

  // Registers `observer` to receive CAPACITY_RELEASED commands (a pure wakeup,
  // args_ == nullptr) on the event loop whenever this controller releases
  // capacity. Called once at scheduler construction; schedulers and the
  // controller are process-lifetime objects, so registration is never undone.
  virtual void RegisterCapacityObserver(event::CommandHandler* observer) PURE;

  // Builds the current kNodeState snapshot. `seq` is caller-owned (monotonic).
  [[nodiscard]] virtual auto BuildStateSnapshot(std::string node_id, uint64_t seq) const
      -> node::NodeState PURE;

  // Shared free capacity of a pool; 0 for undeclared pools.
  [[nodiscard]] virtual auto SharedFree(std::string_view pool) const -> uint64_t PURE;
  // Current in-flight count of a task type.
  [[nodiscard]] virtual auto InFlight(std::string_view task_type) const -> uint64_t PURE;
};

using AdmissionControllerPtr = std::unique_ptr<AdmissionController>;
using AdmissionControllerSharedPtr = std::shared_ptr<AdmissionController>;

// RAII handle releasing admitted capacity when the owning result sender is
// destroyed (a task that never reports a final result still releases its
// reservation) and on the final result. Release() is idempotent. Owns a
// shared_ptr to the controller so capacity can be released even if the scope
// (retained by a long-lived task handler) outlives the call site.
class AdmissionScope {
public:
  AdmissionScope(AdmissionControllerSharedPtr controller, std::string task_type,
                 node::ResourceRequirements requirements);
  ~AdmissionScope();

  AdmissionScope(const AdmissionScope&) = delete;
  auto operator=(const AdmissionScope&) -> AdmissionScope& = delete;
  AdmissionScope(AdmissionScope&&) noexcept = delete;
  auto operator=(AdmissionScope&&) noexcept -> AdmissionScope& = delete;

  void Release();

private:
  AdmissionControllerSharedPtr controller_;
  std::string task_type_;
  node::ResourceRequirements requirements_;
  bool released_{false};
};

using AdmissionScopePtr = std::unique_ptr<AdmissionScope>;

} // namespace strij::nodeagent
