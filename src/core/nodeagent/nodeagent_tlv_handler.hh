#pragma once

#include <memory>
#include <string>

#include "src/core/nodeagent/admission_controller.hh"
#include "src/core/nodeagent/task_handler_manager.hh"

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/node/capabilities.pb.h"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/task_handler_manager.hh"

namespace strij::nodeagent {

class NodeagentTlvHandler {
public:
  NodeagentTlvHandler(TaskHandlerManagerSharedPtr manager,
                      std::shared_ptr<const node::NodeCapabilities> capabilities,
                      AdmissionControllerSharedPtr admission);

  // Serializes and writes the kNodeAdvertisement frame to the connection. Must
  // be the first frame sent on every accepted gateway connection.
  void SendAdvertisement(io::Connection& conn);

  void HandleFrame(io::TlvFrame frame, io::Connection& conn);

private:
  // Resolves the hardware requirements for a submitted task from the handler's
  // advertised default_resources (v1: empty when the type is not declared).
  [[nodiscard]] auto resolveRequirements(const std::string& task_type) const
      -> node::ResourceRequirements;
  static void sendTaskRejected(io::Connection& conn, const std::string& task_id,
                               std::string_view reason);

  TaskHandlerManagerSharedPtr manager_;
  std::shared_ptr<const node::NodeCapabilities> capabilities_;
  AdmissionControllerSharedPtr admission_;
};

} // namespace strij::nodeagent
