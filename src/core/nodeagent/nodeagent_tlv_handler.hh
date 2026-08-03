#pragma once

#include <memory>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/nodeagent/task_handler_manager.hh"

namespace strij::nodeagent {

class NodeagentTlvHandler {
public:
  explicit NodeagentTlvHandler(std::shared_ptr<TaskHandlerManager> manager);

  void HandleFrame(strij::io::TlvFrame frame, strij::io::Connection& conn);

private:
  std::shared_ptr<TaskHandlerManager> manager_;
};

} // namespace strij::nodeagent
