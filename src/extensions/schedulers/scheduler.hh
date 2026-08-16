#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "core/config/extensions.pb.h"
#include "core/extensions/factory_context.hh"
#include "core/gateway/node_directory.hh"
#include "core/node/capabilities.pb.h"
#include "core/task/task.pb.h"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"

namespace strij::extensions {

// A task the gateway is considering for scheduling together with its resolved
// hardware requirements. Both members stay valid for the duration of a
// Scheduler::Choose() call.
struct TaskOffer {
  const task::Task* task;
  const node::ResourceRequirements* requirements;
};

// A gateway scheduling policy. Choose() returns a connected node that
// advertises RequiredProtocol() in its scheduling_protocols (or nullptr when no
// eligible node exists).
class Scheduler {
public:
  Scheduler() = default;
  virtual ~Scheduler() = default;

  Scheduler(const Scheduler&) = delete;
  auto operator=(const Scheduler&) -> Scheduler& = delete;
  Scheduler(Scheduler&&) noexcept = delete;
  auto operator=(Scheduler&&) noexcept -> Scheduler& = delete;

  // The scheduling_protocol a candidate node must advertise (v1: "push").
  [[nodiscard]] virtual auto RequiredProtocol() const -> std::string_view PURE;
  virtual auto Choose(gateway::NodeDirectory& dir, const TaskOffer& offer) -> gateway::Node* PURE;
};

using SchedulerPtr = std::unique_ptr<Scheduler>;

class SchedulerFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  SchedulerFactory() = default;
  virtual ~SchedulerFactory() = default;

  SchedulerFactory(const SchedulerFactory&) = delete;
  auto operator=(const SchedulerFactory&) -> SchedulerFactory& = delete;
  SchedulerFactory(SchedulerFactory&&) noexcept = delete;
  auto operator=(SchedulerFactory&&) noexcept -> SchedulerFactory& = delete;

  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> SchedulerPtr PURE;
};

// Loads a scheduler from the gateway config's `scheduler` ExtensionConfig:
// looks up the named factory in Registry<SchedulerFactory>, unpacks its
// typed_config, and creates the instance. Returns InvalidArgumentError when no
// scheduler is configured (null config) and NotFoundError when the name is not
// registered.
auto CreateScheduler(const config::ExtensionConfig* config, FactoryContext& context)
    -> absl::StatusOr<SchedulerPtr>;

} // namespace strij::extensions
