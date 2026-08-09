#include "core/extensions/factory_context.hh"

#include <utility>

namespace strij::extensions {

FactoryContextImpl::FactoryContextImpl(
    event::DispatcherSharedPtr dispatcher,
    std::unique_ptr<::strij::extensions::FunctionResolver> function_resolver)
    : dispatcher_{std::move(dispatcher)}, function_resolver_{std::move(function_resolver)} {
  if (function_resolver_ == nullptr) {
    function_resolver_ = std::make_unique<LocalFunctionResolver>();
  }
}

auto FactoryContextImpl::Dispatcher() -> event::Dispatcher& { return *dispatcher_; }

auto FactoryContextImpl::Logger() -> logging::Logger& { return logging::Logger::GetInstance(); }

auto FactoryContextImpl::FunctionResolver() -> extensions::FunctionResolver& {
  return *function_resolver_;
}

} // namespace strij::extensions
