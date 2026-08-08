#pragma once

#include <memory>

#include "strij/common/pure.hh"
#include "strij/event/dispatcher.hh"
#include "core/extensions/function_resolver.hh"
#include "core/logging/logger.hh"

namespace strij::extensions {

class FactoryContext {
public:
  virtual ~FactoryContext() = default;

  virtual auto Dispatcher() -> event::Dispatcher& PURE;
  // TODO: Is Logger() really needed?
  virtual auto Logger() -> logging::Logger& PURE;
  // Shared resolver for the `function` task parameter; built once at nodeagent
  // startup and shared by all function-consuming task handler factories.
  virtual auto FunctionResolver() -> ::strij::extensions::FunctionResolver& PURE;
};

using FactoryContextPtr = std::unique_ptr<FactoryContext>;

class FactoryContextImpl : public FactoryContext {
public:
  FactoryContextImpl(event::DispatcherSharedPtr dispatcher,
                     std::unique_ptr<::strij::extensions::FunctionResolver> function_resolver = nullptr);

  auto Dispatcher() -> event::Dispatcher& override;
  auto Logger() -> logging::Logger& override;
  auto FunctionResolver() -> ::strij::extensions::FunctionResolver& override;

private:
  event::DispatcherSharedPtr dispatcher_;
  std::unique_ptr<::strij::extensions::FunctionResolver> function_resolver_;
};

} // namespace strij::extensions
