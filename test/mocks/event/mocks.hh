#pragma once

#include "carrot/event/completable.hh"
#include "carrot/event/dispatcher.hh"
#include "gmock/gmock.h"

namespace carrot::event {

class MockDispatcher : public Dispatcher {
public:
  MOCK_METHOD(void, Run, (), (override));
  MOCK_METHOD(void, Shutdown, (), (override));
  MOCK_METHOD(void, SubmitCommand, (Command cmd), (override));
  MOCK_METHOD(void, PrepareAcceptMultishot, (Completable * io, uint8_t tag, int fd), (override));
  MOCK_METHOD(void, PrepareRead,
              (Completable * io, uint8_t tag, int fd, std::span<std::byte> buf, off_t offset),
              (override));
  MOCK_METHOD(void, PrepareWrite,
              (Completable * io, uint8_t tag, int fd, std::span<const std::byte> buf,
               off_t offset),
              (override));
  MOCK_METHOD(void, PrepareConnect,
              (Completable * io, uint8_t tag, int fd, const struct sockaddr* addr,
               socklen_t addrlen),
              (override));
};

} // namespace carrot::event
