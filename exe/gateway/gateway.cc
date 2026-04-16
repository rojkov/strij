#include "src/core/event/dispatcher_impl.hh"

// TODO:
// make an Application class derived from io_object_t to handle completions in
// the io_uring.

int main() {
  carrot::event::DispatcherPtr dispatcher = std::make_unique<carrot::event::DispatcherImpl>();
  dispatcher->Run();

  return 0;
}
