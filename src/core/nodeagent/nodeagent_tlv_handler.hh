#pragma once

#include <memory>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/nodeagent/task_handler_manager.hh"

namespace carrot::nodeagent {

class NodeagentTlvHandler {
public:
  explicit NodeagentTlvHandler(std::shared_ptr<TaskHandlerManager> manager);

  void HandleFrame(carrot::io::TlvFrame frame, carrot::io::Connection& conn);

private:
  std::shared_ptr<TaskHandlerManager> manager_;
};

} // namespace carrot::nodeagent
