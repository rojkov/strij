#include "core/event/dispatcher_impl.hh"
#include "gateway/application.hh"

auto main() -> int {
  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  carrot::gateway::Application app(dispatcher);
  carrot::event::Command activate_signal_handling{.destination_ = &app};

  // TODO: we can submit this command from the constructor of Application.
  dispatcher->SubmitCommand(activate_signal_handling);
  dispatcher->Run();

  return 0;
}
