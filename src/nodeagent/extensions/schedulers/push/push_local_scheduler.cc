#include "nodeagent/extensions/schedulers/push/push_local_scheduler.hh"

#include <memory>
#include <span>
#include <string_view>

#include "absl/status/status.h"
#include "strij/extensions/extension_registry.hh"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "nodeagent/core/run_task_service.hh"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/schedulers/push/push.pb.h"
#include "strij/extensions/scheduler.hh"

namespace strij::nodeagent::schedulers {

void PushLocalScheduler::Schedule(const task::Task& /*task*/, gateway::ResultReceiverPtr receiver) {
  // The push protocol never schedules outbound: gateways pick nodes, a node
  // cannot push tasks elsewhere. Resolve the receiver (which could otherwise
  // hang) so a hypothetical caller can never leak it.
  receiver->DeliverError("push is a local-only scheduler; it never schedules outbound tasks");
}

auto PushLocalScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

auto PushLocalScheduler::HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status {
  task::Task task;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (!task.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                           static_cast<int>(frame.value.size()))) {
    LOG_WARNING("Malformed Task frame dropped");
    return absl::InvalidArgumentError("malformed Task frame dropped");
  }
  run_task_service_.RunTask(task, conn);
  return absl::OkStatus();
}

auto PushLocalScheduler::HandledFrameTypes() const -> std::span<const uint8_t> {
  return {&io::TlvFrame::kTaskSubmission, 1};
}

auto PushLocalSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::schedulers::PushSchedulerConfig>();
}

auto PushLocalSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                       extensions::NodeagentFactoryContext& context) -> extensions::SchedulerPtr {
  return std::make_unique<PushLocalScheduler>(context.RunTaskService());
}

} // namespace strij::nodeagent::schedulers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::nodeagent::schedulers::PushLocalSchedulerFactory,
                                 strij::nodeagent::NodeSchedulerFactory,
                                 push_local_scheduler_registrar)