#pragma once

#include <memory>
#include <string>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/node/capabilities.pb.h"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/task_handler_manager.hh"

namespace strij::nodeagent {

class NodeagentTlvHandler {
public:
  NodeagentTlvHandler(std::shared_ptr<TaskHandlerManager> manager,
                      std::shared_ptr<const strij::node::NodeCapabilities> capabilities,
                      std::shared_ptr<AdmissionController> admission);

  // Serializes and writes the kNodeAdvertisement frame to the connection. Must
  // be the first frame sent on every accepted gateway connection.
  void SendAdvertisement(strij::io::Connection& conn);

  void HandleFrame(strij::io::TlvFrame frame, strij::io::Connection& conn);

private:
  // Resolves the hardware requirements for a submitted task from the handler's
  // advertised default_resources (v1: empty when the type is not declared).
  auto resolveRequirements(const std::string& task_type) const
      -> strij::node::ResourceRequirements;
  void sendTaskRejected(strij::io::Connection& conn, const std::string& task_id,
                        std::string_view reason);

  std::shared_ptr<TaskHandlerManager> manager_;
  std::shared_ptr<const strij::node::NodeCapabilities> capabilities_;
  std::shared_ptr<AdmissionController> admission_;
};

} // namespace strij::nodeagent
