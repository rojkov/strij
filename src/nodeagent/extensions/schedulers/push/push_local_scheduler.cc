#include "nodeagent/extensions/schedulers/push/push_local_scheduler.hh"

#include <memory>
#include <span>
#include <string_view>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/schedulers/push/push.pb.h"
#include "strij/extensions/extension_registry.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent::schedulers {

void PushLocalScheduler::Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) {
  // The push entry is a pure wire-protocol counterpart: it never runs
  // locally-originated children (that is the bundled "default" scheduler's
  // job). Honor the resolve contract so a misrouted child can never hang its
  // parent.
  LOG_WARNING("Unimplemented: push local scheduler cannot run child task '{}' of type '{}' "
              "(configure the \"default\" scheduler as the local authority)",
              task.id(), task.type());
  receiver->DeliverError(
      "unimplemented: configure the \"default\" scheduler as the local authority for child tasks");
}

auto PushLocalScheduler::RequiredProtocol() const -> std::string_view { return "push"; }

auto PushLocalScheduler::HandleFrame(const io::TlvFrame& frame, io::Connection& conn)
    -> absl::Status {
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
                                       const NodeSchedulerDeps& deps) -> extensions::SchedulerPtr {
  return std::make_unique<PushLocalScheduler>(deps.run_task_service_);
}

} // namespace strij::nodeagent::schedulers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::nodeagent::schedulers::PushLocalSchedulerFactory,
                                 strij::nodeagent::NodeSchedulerFactory,
                                 push_local_scheduler_registrar)