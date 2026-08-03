#pragma once

#include <memory>

#include "strij/common/pure.hh"
#include "strij/event/dispatcher.hh"
#include "core/logging/logger.hh"

namespace strij::extensions {

class FactoryContext {
public:
  virtual ~FactoryContext() = default;

  virtual auto Dispatcher() -> event::Dispatcher& PURE;
  // TODO: Is Logger() really needed?
  virtual auto Logger() -> logging::Logger& PURE;
};

using FactoryContextPtr = std::unique_ptr<FactoryContext>;

class FactoryContextImpl : public FactoryContext {
public:
  explicit FactoryContextImpl(event::DispatcherSharedPtr dispatcher);

  auto Dispatcher() -> event::Dispatcher& override;
  auto Logger() -> logging::Logger& override;

private:
  event::DispatcherSharedPtr dispatcher_;
};

} // namespace strij::extensions
