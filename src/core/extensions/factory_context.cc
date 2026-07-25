#include "core/extensions/factory_context.hh"

namespace carrot::extensions {

GatewayFactoryContext::GatewayFactoryContext(event::DispatcherSharedPtr dispatcher)
    : dispatcher_(std::move(dispatcher)) {}

auto GatewayFactoryContext::Dispatcher() -> event::Dispatcher& {
  return *dispatcher_;
}

auto GatewayFactoryContext::Logger() -> logging::Logger& {
  return logging::Logger::GetInstance();
}

} // namespace carrot::extensions
