#pragma once

#include "carrot/event/dispatcher.hh"
#include "gmock/gmock.h"

namespace carrot::event {

class MockDispatcher : public Dispatcher {
public:
  MOCK_METHOD(void, Run, (), (override));
  MOCK_METHOD(void, Shutdown, (), (override));
  MOCK_METHOD(void, SubmitCommand, (Command cmd), (override));
  MOCK_METHOD(void, PrepareAcceptMultishot, (IOObject * io_object, uint8_t tag, int fd),
              (override));
  MOCK_METHOD(void, PrepareRead,
              (IOObject * io_object, uint8_t tag, int fd, std::span<std::byte> buf, off_t offset),
              (override));
  MOCK_METHOD(void, PrepareWrite,
              (IOObject * io_object, uint8_t tag, int fd, std::span<const std::byte> buf,
               off_t offset),
              (override));
  MOCK_METHOD(void, PrepareConnect,
              (IOObject * io_object, uint8_t tag, int fd, const struct sockaddr* addr,
               socklen_t addrlen),
              (override));
};

} // namespace carrot::event
