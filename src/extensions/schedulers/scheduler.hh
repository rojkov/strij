#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "google/protobuf/message.h"

#include "strij/common/pure.hh"
#include "core/config/extensions.pb.h"
#include "core/extensions/factory_context.hh"
#include "core/extensions/extension_registry.hh"
#include "core/gateway/node_directory.hh"
#include "core/node/capabilities.pb.h"
#include "core/task/task.pb.h"

namespace strij::extensions {

// A task the gateway is considering for scheduling together with its resolved
// hardware requirements. Both members stay valid for the duration of a
// Scheduler::Choose() call.
struct TaskOffer {
  const strij::task::Task* task;
  const strij::node::ResourceRequirements* requirements;
};

// A gateway scheduling policy. Choose() returns a connected node that
// advertises RequiredProtocol() in its scheduling_protocols (or nullptr when no
// eligible node exists).
class Scheduler {
public:
  virtual ~Scheduler() = default;

  // The scheduling_protocol a candidate node must advertise (v1: "push").
  virtual auto RequiredProtocol() const -> std::string_view PURE;
  virtual auto Choose(strij::gateway::NodeDirectory& dir, const TaskOffer& offer)
      -> strij::gateway::Node* PURE;
};

class SchedulerFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  virtual ~SchedulerFactory() = default;
  virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<Scheduler> PURE;
};

// Loads a scheduler from the gateway config's `scheduler` ExtensionConfig:
// looks up the named factory in Registry<SchedulerFactory>, unpacks its
// typed_config, and creates the instance. Returns InvalidArgumentError when no
// scheduler is configured (null config) and NotFoundError when the name is not
// registered.
auto CreateScheduler(const strij::config::ExtensionConfig* config, FactoryContext& context)
    -> absl::StatusOr<std::unique_ptr<Scheduler>>;

} // namespace strij::extensions
