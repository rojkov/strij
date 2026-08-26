#include "extensions/schedulers/round_robin/round_robin_scheduler.hh"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/extensions/extension_registry.hh"
#include "core/extensions/factory_context.hh"
#include "core/gateway/node.hh"
#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/task/task.pb.h"
#include "extensions/schedulers/round_robin/round_robin.pb.h"
#include "extensions/schedulers/scheduler.hh"

namespace strij::extensions::schedulers {

auto RoundRobinScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

auto RoundRobinScheduler::choose(gateway::NodeDirectory& dir) -> gateway::Node* {
  std::vector<gateway::Node*> candidates = dir.GetCandidates(RequiredProtocol());
  if (candidates.empty()) {
    return nullptr;
  }

  gateway::Node* chosen = candidates.at(next_index_ % candidates.size());
  next_index_ = (next_index_ + 1) % candidates.size();

  return chosen;
}

void RoundRobinScheduler::Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) {
  gateway::Node* node = choose(directory_);
  if (node == nullptr) {
    receiver->DeliverError("no available node for task submission");
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

auto RoundRobinSchedulerFactory::Name() const -> std::string { return "round_robin"; }

auto RoundRobinSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::schedulers::round_robin::RoundRobinSchedulerConfig>();
}

auto RoundRobinSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                        GatewayFactoryContext& context) -> SchedulerPtr {
  return std::make_unique<RoundRobinScheduler>(context.NodeDirectory(),
                                               context.ResultReceiverStorage());
}

} // namespace strij::extensions::schedulers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::extensions::schedulers::RoundRobinSchedulerFactory,
                                 strij::extensions::GatewaySchedulerFactory,
                                 round_robin_scheduler_registrar)