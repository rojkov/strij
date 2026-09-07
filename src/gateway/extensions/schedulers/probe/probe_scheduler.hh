#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/core/io/periodic_timer.hh"
#include "common/task/task.pb.h"
#include "gateway/core/node.hh"
#include "google/protobuf/message.h"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/node_directory.hh"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij::gateway::schedulers::probe {

// The gateway half of Sparrow-style probe scheduling. Schedule() samples a
// deterministic (task.id-seeded) subset of the probe-eligible nodes — at most
// `candidate_count`, clamped to what GetCandidates("probe") returns — and sends
// a kTaskProbe to each. First pull wins:
//
//   - kTaskPull → grant the full Task to the pulling node, kTaskProbeCancel the
//     other probed nodes, transfer the receiver into ResultReceiverStorage (so
//     subsequent kResult/kTaskRejected frames route normally), erase probe state.
//     A pull for an id with no probe state (late, or from an unprobed node) gets
//     only a kTaskProbeCancel — a revoked dup can never double-grant.
//   - kTaskDecline → drop the declining node; when the last outstanding node
//     declines, DeliverError(reason) and erase (no deadline wait).
//   - deadline (probe_deadline) → swept by an internal PeriodicTimer (~100ms):
//     cancel all outstanding nodes and DeliverError. Every task handed to
//     Schedule resolves through its receiver, never hanging.
class ProbeScheduler final : public extensions::Scheduler {
public:
  // Injectable clock so deadline tests don't depend on wall time; defaults to
  // steady_clock::now.
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  ProbeScheduler(
      gateway::NodeDirectory& directory, gateway::ResultReceiverStorage& storage,
      event::DispatcherSharedPtr dispatcher, size_t candidate_count,
      std::chrono::milliseconds probe_deadline,
      Clock clock = [] { return std::chrono::steady_clock::now(); });
  ~ProbeScheduler() override = default;

  ProbeScheduler(const ProbeScheduler&) = delete;
  auto operator=(const ProbeScheduler&) -> ProbeScheduler& = delete;
  ProbeScheduler(ProbeScheduler&&) noexcept = delete;
  auto operator=(ProbeScheduler&&) noexcept -> ProbeScheduler& = delete;

  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
  auto HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status override;
  [[nodiscard]] auto HandledFrameTypes() const -> std::span<const uint8_t> override;

  // Expires overdue probes (cancel + error). Driven by the internal timer; made
  // public so tests can drive the sweep with a fake clock.
  void SweepExpired();

private:
  // In-flight probe round, keyed by task.id. Holds the receiver until a pull
  // wins (then it moves into ResultReceiverStorage) or the round resolves.
  struct PendingProbe {
    task::Task task;
    gateway::ResultReceiverPtr receiver;
    std::vector<std::string> probed_node_ids;
    std::chrono::steady_clock::time_point deadline;
  };

  static auto owningNode(io::Connection& conn) -> gateway::Node* {
    return dynamic_cast<gateway::Node*>(conn.GetOwner());
  }

  static void sendProbe(gateway::Node& node, const task::Task& task);
  static void sendGrant(io::Connection& conn, const task::Task& task);
  static void sendCancel(gateway::Node& node, const std::string& task_id);
  static void sendCancelOnConnection(io::Connection& conn, const std::string& task_id);

  auto handleTaskPullFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status;
  auto handleTaskDeclineFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status;

  gateway::NodeDirectory& directory_;
  gateway::ResultReceiverStorage& storage_;
  event::DispatcherSharedPtr dispatcher_;
  size_t candidate_count_;
  std::chrono::milliseconds probe_deadline_;
  Clock clock_;
  io::PeriodicTimer timer_;
  std::unordered_map<std::string, PendingProbe> pending_;
};

class ProbeSchedulerFactory final : public GatewaySchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, extensions::GatewayFactoryContext& context)
      -> extensions::SchedulerPtr override;
};

} // namespace strij::gateway::schedulers::probe