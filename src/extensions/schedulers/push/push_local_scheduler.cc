#include "extensions/schedulers/push/push_local_scheduler.hh"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core/extensions/extension_registry.hh"
#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/nodeagent/run_task_service.hh"
#include "core/task/task.pb.h"
#include "extensions/schedulers/push/push.pb.h"
#include "extensions/schedulers/scheduler.hh"

namespace strij::extensions::schedulers {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
const auto registered = [] {
  Registry<NodeSchedulerFactory>::instance().RegisterFactory(
      PushLocalSchedulerFactory().Name(), new PushLocalSchedulerFactory());
  return true;
}();

} // namespace

void PushLocalScheduler::Schedule(const task::Task& /*task*/,
                                  gateway::ResultReceiverPtr receiver) {
  // The push protocol never schedules outbound: gateways pick nodes, a node
  // cannot push tasks elsewhere. Resolve the receiver (which could otherwise
  // hang) so a hypothetical caller can never leak it.
  receiver->DeliverError("push is a local-only scheduler; it never schedules outbound tasks");
}

auto PushLocalScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

void PushLocalScheduler::HandleFrame(io::TlvFrame frame, io::Connection& conn) {
  task::Task task;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (!task.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                           static_cast<int>(frame.value.size()))) {
    LOG_WARNING("Malformed Task frame dropped");
    return;
  }
  run_task_service_.RunTask(task, conn);
}

auto PushLocalScheduler::HandledFrameTypes() const -> std::span<const uint8_t> {
  return std::span<const uint8_t>(&io::TlvFrame::kTaskSubmission, 1);
}

auto PushLocalSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<PushSchedulerConfig>();
}

auto PushLocalSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                       NodeagentFactoryContext& context) -> SchedulerPtr {
  return std::make_unique<PushLocalScheduler>(context.RunTaskService());
}

} // namespace strij::extensions::schedulers