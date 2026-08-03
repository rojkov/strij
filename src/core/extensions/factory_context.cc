#include "core/extensions/factory_context.hh"

namespace strij::extensions {

FactoryContextImpl::FactoryContextImpl(event::DispatcherSharedPtr dispatcher)
    : dispatcher_(std::move(dispatcher)) {}

auto FactoryContextImpl::Dispatcher() -> event::Dispatcher& {
  return *dispatcher_;
}

auto FactoryContextImpl::Logger() -> logging::Logger& {
  return logging::Logger::GetInstance();
}

} // namespace strij::extensions
