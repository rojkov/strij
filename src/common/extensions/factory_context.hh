#pragma once

#include <memory>

#include "common/core/logging/logger.hh"
#include "strij/common/pure.hh"
#include "strij/event/dispatcher.hh"

namespace strij::gateway {

class NodeDirectory;
class ResultReceiverStorage;

} // namespace strij::gateway

namespace strij::nodeagent {

class FunctionResolver;
class AdmissionController;
class RunTaskService;
using AdmissionControllerSharedPtr = std::shared_ptr<AdmissionController>;

} // namespace strij::nodeagent

namespace strij::extensions {

// Base services shared by every extension factory category. Factory contexts
// are constructed once per process and handed to extension factories at Create;
// extensions must not retain the context past their owner's lifetime.
class FactoryContext {
public:
  FactoryContext() = default;
  virtual ~FactoryContext() = default;

  FactoryContext(const FactoryContext&) = delete;
  auto operator=(const FactoryContext&) -> FactoryContext& = delete;
  FactoryContext(FactoryContext&&) noexcept = delete;
  auto operator=(FactoryContext&&) noexcept -> FactoryContext& = delete;

  virtual auto Dispatcher() -> event::Dispatcher& PURE;
  // TODO: Is Logger() really needed?
  virtual auto Logger() -> logging::Logger& PURE;
};

// Gateway-side extension services. Only gateway scheduler extensions and node
// discovery extensions are created with this context.
class GatewayFactoryContext : public FactoryContext {
public:
  virtual auto NodeDirectory() -> gateway::NodeDirectory& PURE;
  virtual auto ResultReceiverStorage() -> gateway::ResultReceiverStorage& PURE;
};

// Nodeagent-side extension services. Only nodeagent scheduler extensions and
// task handler extensions are created with this context.
class NodeagentFactoryContext : public FactoryContext {
public:
  virtual auto FunctionResolver() -> nodeagent::FunctionResolver& PURE;
  virtual auto AdmissionController() -> nodeagent::AdmissionControllerSharedPtr PURE;
  virtual auto RunTaskService() -> nodeagent::RunTaskService& PURE;
};

} // namespace strij::extensions