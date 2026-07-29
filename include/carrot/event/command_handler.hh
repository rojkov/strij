#pragma once

#include "carrot/common/pure.hh"
#include "carrot/event/command.hh"

namespace carrot::event {

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

} // namespace carrot::event
