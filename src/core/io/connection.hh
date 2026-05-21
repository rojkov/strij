#pragma once

#include <array>
#include <functional>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"
#include "core/io/llhttp_parser.hh"

namespace carrot::io {

class Connection final {
public:
  Connection(int connection_fd, event::DispatcherSharedPtr dispatcher);

private:
  void onEndOfStream();

  int fd_;
  event::DispatcherSharedPtr dispatcher_;
  std::unique_ptr<LlhttpParser> parser_;
  std::string response_;
};

} // namespace carrot::io
