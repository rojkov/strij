#pragma once

#include "carrot/event/dispatcher.hh"
#include "gmock/gmock.h"

namespace carrot::event {

class MockDispatcher : public Dispatcher {
public:
  MOCK_METHOD(void, Run, (), (override));
  MOCK_METHOD(void, Shutdown, (), (override));
  MOCK_METHOD(void, SubmitCommand, (Command cmd), (override));
  MOCK_METHOD(void, PrepareAcceptMultishot, (IOObject * io_object, int fd), (override));
  MOCK_METHOD(void, PrepareRead,
              (IOObject * io_object, int fd, std::span<std::byte> buf, off_t offset), (override));
};

} // namespace carrot::event
