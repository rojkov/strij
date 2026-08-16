#include "extensions/schedulers/round_robin/round_robin_scheduler.hh"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/extensions/extension_registry.hh"
#include "core/extensions/factory_context.hh"
#include "core/gateway/node.hh"
#include "core/gateway/node_directory.hh"
#include "extensions/schedulers/round_robin/round_robin.pb.h"
#include "extensions/schedulers/scheduler.hh"

namespace strij::extensions::schedulers {

auto RoundRobinScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

auto RoundRobinScheduler::Choose(gateway::NodeDirectory& dir, const TaskOffer& /*offer*/)
    -> gateway::Node* {
  std::vector<gateway::Node*> candidates = dir.GetCandidates(RequiredProtocol());
  if (candidates.empty()) {
    return nullptr;
  }

  gateway::Node* chosen = candidates.at(next_index_ % candidates.size());
  next_index_ = (next_index_ + 1) % candidates.size();

  return chosen;
}

auto RoundRobinSchedulerFactory::Name() const -> std::string { return "round_robin"; }

auto RoundRobinSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::schedulers::round_robin::RoundRobinSchedulerConfig>();
}

auto RoundRobinSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                        FactoryContext& /*context*/) -> SchedulerPtr {
  return std::make_unique<RoundRobinScheduler>();
}

} // namespace strij::extensions::schedulers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::extensions::schedulers::RoundRobinSchedulerFactory,
                                 strij::extensions::SchedulerFactory,
                                 round_robin_scheduler_registrar)
