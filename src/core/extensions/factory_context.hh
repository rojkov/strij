#pragma once

#include <memory>

#include "carrot/event/dispatcher.hh"
#include "core/logging/logger.hh"

namespace carrot::extensions {

class FactoryContext {
public:
  virtual ~FactoryContext() = default;

  virtual auto Dispatcher() -> event::Dispatcher& = 0;
  virtual auto Logger() -> logging::Logger& = 0;
};

using FactoryContextPtr = std::unique_ptr<FactoryContext>;

class GatewayFactoryContext : public FactoryContext {
public:
  explicit GatewayFactoryContext(event::DispatcherSharedPtr dispatcher);

  auto Dispatcher() -> event::Dispatcher& override;
  auto Logger() -> logging::Logger& override;

private:
  event::DispatcherSharedPtr dispatcher_;
};

} // namespace carrot::extensions
