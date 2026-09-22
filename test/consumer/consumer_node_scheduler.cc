#include "consumer_node_scheduler.hh"

#include <memory>
#include <string>

#include "common/config/extensions.pb.h"
#include "strij/extensions/extension_registry.hh"

namespace strij_consumer {

void ConsumerNodeScheduler::Schedule(const strij::task::Task& /*task*/,
                                     strij::gateway::ResultReceiverPtr receiver) {
  receiver->DeliverError("consumer_node_scheduler: node-side schedulers own inbound frames, "
                         "not gateway scheduling; rejecting as unsupported");
}

auto ConsumerNodeScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

auto ConsumerNodeSchedulerFactory::Name() const -> std::string {
  return std::string(kConsumerNodeSchedulerName);
}

auto ConsumerNodeSchedulerFactory::RequiredProtocol() const -> std::string_view { return "push"; }

auto ConsumerNodeSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<strij::config::ExtensionConfig>();
}

auto ConsumerNodeSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                          const strij::nodeagent::NodeSchedulerDeps& /*deps*/)
    -> strij::extensions::SchedulerPtr {
  return std::make_unique<ConsumerNodeScheduler>();
}

} // namespace strij_consumer

REGISTER_FACTORY_FULLY_QUALIFIED(strij_consumer::ConsumerNodeSchedulerFactory,
                                 ::strij::nodeagent::NodeSchedulerFactory,
                                 consumer_node_scheduler_registrar)