#include "gateway/extensions/schedulers/probe/probe_scheduler.hh"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "common/task/probe.pb.h"
#include "common/task/task.pb.h"
#include "gateway/extensions/schedulers/probe/probe.pb.h"
#include "strij/extensions/extension_registry.hh"
#include "strij/extensions/scheduler.hh"

namespace strij::gateway::schedulers::probe {

namespace {

constexpr size_t kDefaultCandidateCount = 2;
constexpr uint32_t kDefaultProbeDeadlineMs = 1000;
constexpr absl::Duration kSweepInterval = absl::Milliseconds(100);

auto parseMessage(const io::TlvFrame& frame, google::protobuf::Message& message) -> bool {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return message.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                                static_cast<int>(frame.value.size()));
}

auto serializeTaskProbe(const task::Task& task) -> std::vector<std::byte> {
  task::TaskProbe probe;
  probe.set_id(task.id());
  probe.set_type(task.type());
  probe.mutable_requirements()->CopyFrom(task.requirements());
  std::string serialized;
  probe.SerializeToString(&serialized);
  return io::SerializeTlvFrame(io::TlvFrame::kTaskProbe,
                               std::as_bytes(std::span(serialized.data(), serialized.size())));
}

auto serializeCancel(const std::string& id) -> std::vector<std::byte> {
  task::TaskProbeCancel cancel;
  cancel.set_id(id);
  std::string serialized;
  cancel.SerializeToString(&serialized);
  return io::SerializeTlvFrame(io::TlvFrame::kTaskProbeCancel,
                               std::as_bytes(std::span(serialized.data(), serialized.size())));
}

auto serializeGrant(const task::Task& task) -> std::vector<std::byte> {
  std::string serialized;
  task.SerializeToString(&serialized);
  return io::SerializeTlvFrame(io::TlvFrame::kTaskGrant,
                               std::as_bytes(std::span(serialized.data(), serialized.size())));
}

// Deterministic sample of `k` nodes without replacement, seeded by `seed` (the
// task id): the same task id and candidate set always produce the same probe
// set, decorrelating successive probes across tasks.
auto sampleCandidates(const std::vector<gateway::Node*>& candidates, size_t k,
                      const std::string& seed) -> std::vector<gateway::Node*> {
  std::vector<gateway::Node*> pool = candidates;
  const size_t count = std::min(k, pool.size());
  std::seed_seq seed_seq(seed.begin(), seed.end());
  std::mt19937 rng(seed_seq);

  for (size_t i = 0; i < count; ++i) {
    std::uniform_int_distribution<size_t> dist(i, pool.size() - 1);
    std::swap(pool[i], pool[dist(rng)]);
  }
  pool.resize(count);
  return pool;
}

} // namespace

ProbeScheduler::ProbeScheduler(gateway::NodeDirectory& directory,
                               gateway::ResultReceiverStorage& storage,
                               event::DispatcherSharedPtr dispatcher, size_t candidate_count,
                               std::chrono::milliseconds probe_deadline, Clock clock)
    : directory_{directory}, storage_{storage}, dispatcher_{std::move(dispatcher)},
      candidate_count_{candidate_count}, probe_deadline_{probe_deadline}, clock_{std::move(clock)},
      timer_{dispatcher_, [this] { SweepExpired(); }} {
  timer_.Start(kSweepInterval);
}

auto ProbeScheduler::RequiredProtocol() const -> std::string_view { return "probe"; }

void ProbeScheduler::sendProbe(gateway::Node& node, const task::Task& task) {
  LOG_DEBUG("Probing node {} for task {}", node.GetNodeId(), task.id());
  node.GetConnection()->Write(serializeTaskProbe(task));
}

void ProbeScheduler::sendGrant(io::Connection& conn, const task::Task& task) {
  LOG_DEBUG("Granting task {} on the winning node connection", task.id());
  conn.Write(serializeGrant(task));
}

void ProbeScheduler::sendCancel(gateway::Node& node, const std::string& id) {
  LOG_DEBUG("Cancelling probe of task {} at node {}", id, node.GetNodeId());
  node.GetConnection()->Write(serializeCancel(id));
}

void ProbeScheduler::sendCancelOnConnection(io::Connection& conn, const std::string& id) {
  LOG_DEBUG("Revoking pull for task {} (no pending probe round)", id);
  conn.Write(serializeCancel(id));
}

void ProbeScheduler::Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) {
  const std::vector<gateway::Node*> candidates = directory_.GetCandidates(RequiredProtocol());
  const size_t effective_k = std::min(candidate_count_, candidates.size());
  if (effective_k == 0) {
    receiver->DeliverError("no probe-eligible nodes available");
    return;
  }

  std::vector<gateway::Node*> sampled = sampleCandidates(candidates, effective_k, task.id());

  PendingProbe pending;
  pending.task = task;
  pending.receiver = std::move(receiver);
  pending.deadline = clock_() + probe_deadline_;
  for (const auto* node : sampled) {
    pending.probed_node_ids.emplace_back(node->GetNodeId());
  }
  pending_.insert_or_assign(task.id(), std::move(pending));

  for (auto* node : sampled) {
    sendProbe(*node, task);
  }
}

auto ProbeScheduler::HandleFrame(const io::TlvFrame& frame, io::Connection& conn)
    -> absl::Status {
  switch (frame.type_id) {
  case io::TlvFrame::kTaskPull: {
    task::TaskPull pull;
    if (!parseMessage(frame, pull)) {
      LOG_WARNING("Malformed TaskPull frame dropped");
      return absl::InvalidArgumentError("malformed TaskPull frame dropped");
    }

    auto it = pending_.find(pull.id());
    if (it == pending_.end()) {
      // Late or duplicate pull after the round resolved: revoke, never grant.
      sendCancelOnConnection(conn, pull.id());
      return absl::OkStatus();
    }

    gateway::Node* winner = owningNode(conn);
    if (winner == nullptr) {
      return absl::InternalError("pull on a connection without an owning Node");
    }

    auto& probed = it->second.probed_node_ids;
    if (std::find(probed.begin(), probed.end(), winner->GetNodeId()) == probed.end()) {
      // The pulling node was not one of our candidates (e.g. it already
      // declined): revoke its hold.
      sendCancelOnConnection(conn, pull.id());
      return absl::OkStatus();
    }

    // First pull wins. Grant the winner, cancel every other outstanding node.
    sendGrant(conn, it->second.task);
    for (const auto& node_id : probed) {
      if (node_id != winner->GetNodeId()) {
        if (gateway::Node* loser = directory_.GetNode(node_id); loser != nullptr) {
          sendCancel(*loser, pull.id());
        }
      }
    }

    // The winner's future kResult/kTaskRejected frames route via storage.
    storage_.Put(pull.id(), std::move(it->second.receiver), winner->GetNodeId());
    pending_.erase(it);
    return absl::OkStatus();
  }
  case io::TlvFrame::kTaskDecline: {
    task::TaskDecline decline;
    if (!parseMessage(frame, decline)) {
      LOG_WARNING("Malformed TaskDecline frame dropped");
      return absl::InvalidArgumentError("malformed TaskDecline frame dropped");
    }

    auto it = pending_.find(decline.id());
    if (it == pending_.end()) {
      // Round already resolved (grant, timeout, or all-declined): ignore.
      return absl::OkStatus();
    }

    gateway::Node* decliner = owningNode(conn);
    if (decliner != nullptr) {
      auto& probed = it->second.probed_node_ids;
      probed.erase(std::remove(probed.begin(), probed.end(), decliner->GetNodeId()), probed.end());
    }

    if (it->second.probed_node_ids.empty()) {
      LOG_WARNING("All probed nodes declined task {}: {}", decline.id(), decline.reason());
      it->second.receiver->DeliverError(decline.reason());
      pending_.erase(it);
    }
    return absl::OkStatus();
  }
  default:
    return absl::NotFoundError("probe scheduler does not own TLV type_id " +
                              std::to_string(frame.type_id));
  }
}

void ProbeScheduler::SweepExpired() {
  const auto now = clock_();
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (now <= it->second.deadline) {
      ++it;
      continue;
    }

    for (const auto& node_id : it->second.probed_node_ids) {
      if (gateway::Node* node = directory_.GetNode(node_id); node != nullptr) {
        sendCancel(*node, it->first);
      }
    }
    it->second.receiver->DeliverError("no node claimed the task within the probe deadline");
    it = pending_.erase(it);
  }
}

auto ProbeScheduler::HandledFrameTypes() const -> std::span<const uint8_t> {
  static constexpr uint8_t kTypes[] = {io::TlvFrame::kTaskPull, io::TlvFrame::kTaskDecline};
  return kTypes;
}

auto ProbeSchedulerFactory::Name() const -> std::string { return "probe"; }

auto ProbeSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::schedulers::probe::ProbeRoleSchedulerConfig>();
}

auto ProbeSchedulerFactory::Create(const ::google::protobuf::Message& config,
                                   extensions::GatewayFactoryContext& context)
    -> extensions::SchedulerPtr {
  const auto* typed =
      dynamic_cast<const extensions::schedulers::probe::ProbeRoleSchedulerConfig*>(&config);
  if (typed == nullptr) {
    LOG_ERROR("ProbeSchedulerFactory: typed_config is not a ProbeRoleSchedulerConfig");
    return nullptr;
  }

  const size_t candidate_count =
      typed->has_candidate_count() ? typed->candidate_count() : kDefaultCandidateCount;
  if (candidate_count == 0) {
    LOG_ERROR("ProbeRoleSchedulerConfig: candidate_count must be >= 1");
    return nullptr;
  }

  const std::chrono::milliseconds probe_deadline(
      typed->has_probe_deadline_ms() ? typed->probe_deadline_ms() : kDefaultProbeDeadlineMs);

  return std::make_unique<ProbeScheduler>(context.NodeDirectory(),
                                          context.ResultReceiverStorage(),
                                          context.SharedDispatcher(), candidate_count,
                                          probe_deadline);
}

} // namespace strij::gateway::schedulers::probe

REGISTER_FACTORY_FULLY_QUALIFIED(strij::gateway::schedulers::probe::ProbeSchedulerFactory,
                                 strij::gateway::GatewaySchedulerFactory,
                                 probe_scheduler_registrar)