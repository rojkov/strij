#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "common/node/capabilities.pb.h"
#include "strij/event/command_handler.hh"
#include "strij/event/dispatcher.hh"
#include "strij/nodeagent/admission_controller.hh"

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
class AdmissionControllerImpl final : public AdmissionController {
public:
  AdmissionControllerImpl(const node::NodeCapabilities& capabilities,
                          event::Dispatcher& dispatcher);

  // AdmissionController
  auto Admit(std::string_view task_type, const node::ResourceRequirements& requirements)
      -> absl::Status override;
  void Release(std::string_view task_type, const node::ResourceRequirements& requirements) override;
  [[nodiscard]] auto BuildStateSnapshot(std::string node_id, uint64_t seq) const
      -> node::NodeState override;
  [[nodiscard]] auto SharedFree(std::string_view pool) const -> uint64_t override;
  [[nodiscard]] auto InFlight(std::string_view task_type) const -> uint64_t override;
  void RegisterCapacityObserver(event::CommandHandler* observer) override;

private:
  event::Dispatcher& dispatcher_;
  std::vector<event::CommandHandler*> capacity_observers_;

  void notifyCapacityReleased();

  // std::less<> enables heterogeneous (string_view) lookups.
  std::map<std::string, uint64_t, std::less<>> pool_total_map_;
  std::map<std::string, uint64_t, std::less<>> pool_reserved_map_;
  std::map<std::string, uint64_t, std::less<>> pool_in_use_map_;
  // 0 means no concurrency limit for the type.
  std::map<std::string, uint64_t, std::less<>> type_concurrency_map_;
  std::map<std::string, uint64_t, std::less<>> type_in_flight_map_;
};

} // namespace strij::nodeagent
