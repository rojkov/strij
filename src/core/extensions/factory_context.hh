#pragma once

#include <memory>

#include "core/extensions/function_resolver.hh"
#include "core/logging/logger.hh"
#include "strij/common/pure.hh"
#include "strij/event/dispatcher.hh"

namespace strij::extensions {

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
  // Shared resolver for the `function` task parameter; built once at nodeagent
  // startup and shared by all function-consuming task handler factories.
  virtual auto FunctionResolver() -> extensions::FunctionResolver& PURE;
};

using FactoryContextPtr = std::unique_ptr<FactoryContext>;

class FactoryContextImpl : public FactoryContext {
public:
  // TODO: Consider making the function_resolver argument non-optional and always passing a resolver
  // (even if it's a LocalFunctionResolver). This would simplify the code and avoid potential null
  // dereferences. This may imply having different factory context implementations for gateway and
  // nodeagent.
  FactoryContextImpl(event::DispatcherSharedPtr dispatcher,
                     std::unique_ptr<extensions::FunctionResolver> function_resolver = nullptr);

  auto Dispatcher() -> event::Dispatcher& override;
  auto Logger() -> logging::Logger& override;
  auto FunctionResolver() -> extensions::FunctionResolver& override;

private:
  event::DispatcherSharedPtr dispatcher_;
  std::unique_ptr<extensions::FunctionResolver> function_resolver_;
};

} // namespace strij::extensions
