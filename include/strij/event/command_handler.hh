#pragma once

#include "strij/common/pure.hh"
#include "strij/event/command.hh"

namespace strij::event {

class CommandHandler {
public:
  CommandHandler() = default;
  virtual ~CommandHandler() = default;

  CommandHandler(const CommandHandler&) = delete;
  auto operator=(const CommandHandler&) -> CommandHandler& = delete;
  CommandHandler(CommandHandler&&) noexcept = delete;
  auto operator=(CommandHandler&&) noexcept -> CommandHandler& = delete;

  virtual void ProcessCommand(Command cmd) PURE;
};

} // namespace strij::event
