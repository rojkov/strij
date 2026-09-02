#include "consumer_scheduler.hh"

#include <memory>
#include <string>
#include <utility>

#include "common/config/extensions.pb.h"
#include "strij/extensions/extension_registry.hh"

namespace strij_consumer {

void ConsumerScheduler::Schedule(const strij::task::Task& /*task*/,
                                 strij::gateway::ResultReceiverPtr receiver) {
  receiver->DeliverError("consumer_scheduler rejected task: extension layout smoke test");
}

auto ConsumerScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

auto ConsumerSchedulerFactory::Name() const -> std::string {
  return std::string(kConsumerSchedulerName);
}

auto ConsumerSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<strij::config::ExtensionConfig>();
}

auto ConsumerSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                      strij::extensions::GatewayFactoryContext& /*context*/)
    -> strij::extensions::SchedulerPtr {
  return std::make_unique<ConsumerScheduler>();
}

} // namespace strij_consumer

REGISTER_FACTORY_FULLY_QUALIFIED(strij_consumer::ConsumerSchedulerFactory,
                                 ::strij::gateway::GatewaySchedulerFactory,
                                 consumer_scheduler_registrar)