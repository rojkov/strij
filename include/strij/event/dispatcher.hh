#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <sys/socket.h>

#include "strij/common/pure.hh"
#include "strij/event/completable.hh"
#include "strij/event/command.hh"

namespace strij::event {

class Dispatcher {
public:
  Dispatcher() = default;
  virtual ~Dispatcher() = default;

  Dispatcher(const Dispatcher&) = delete;
  auto operator=(const Dispatcher&) -> Dispatcher& = delete;
  Dispatcher(Dispatcher&&) noexcept = delete;
  auto operator=(Dispatcher&&) noexcept -> Dispatcher& = delete;

  virtual void Run() PURE;
  virtual void Shutdown() PURE;
  virtual void SubmitCommand(Command cmd) PURE;
  // tag is interpreted by the receiver Completable.
  virtual void PrepareAcceptMultishot(Completable* io, uint8_t tag, int fd) PURE;
  virtual void PrepareRead(Completable* io, uint8_t tag, int fd, std::span<std::byte> buf,
                           off_t offset) PURE;
  virtual void PrepareWrite(Completable* io, uint8_t tag, int fd,
                            std::span<const std::byte> buf, off_t offset) PURE;
  virtual void PrepareConnect(Completable* io, uint8_t tag, int fd,
                              const struct sockaddr* addr, socklen_t addrlen) PURE;
  virtual void PreparePoll(Completable* io, uint8_t tag, int fd, uint32_t poll_mask) PURE;
};

using DispatcherSharedPtr = std::shared_ptr<Dispatcher>;

} // namespace strij::event
