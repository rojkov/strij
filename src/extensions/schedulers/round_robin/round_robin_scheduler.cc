#include "extensions/schedulers/round_robin/round_robin_scheduler.hh"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "extensions/schedulers/round_robin/round_robin.pb.h"

namespace strij::extensions::schedulers {

auto RoundRobinScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

auto RoundRobinScheduler::Choose(strij::gateway::NodeDirectory& dir,
                                 const TaskOffer& /*offer*/) -> strij::gateway::Node* {
  std::vector<strij::gateway::Node*> candidates = dir.GetCandidates(RequiredProtocol());
  if (candidates.empty()) {
    return nullptr;
  }
  strij::gateway::Node* chosen = candidates[next_index_ % candidates.size()];
  next_index_ = (next_index_ + 1) % candidates.size();
  return chosen;
}

auto RoundRobinSchedulerFactory::Name() const -> std::string { return "round_robin"; }

auto RoundRobinSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<
      strij::extensions::schedulers::round_robin::RoundRobinSchedulerConfig>();
}

auto RoundRobinSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                        FactoryContext& /*context*/)
    -> std::unique_ptr<Scheduler> {
  return std::make_unique<RoundRobinScheduler>();
}

} // namespace strij::extensions::schedulers

REGISTER_FACTORY_FULLY_QUALIFIED(
    strij::extensions::schedulers::RoundRobinSchedulerFactory, strij::extensions::SchedulerFactory,
    round_robin_scheduler_registrar)
