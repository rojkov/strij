#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "carrot/event/dispatcher.hh"
#include "core/io/connection.hh"
#include "core/io/node.hh"

namespace carrot::io {

class NodeDirectory {
public:
  NodeDirectory(event::DispatcherSharedPtr dispatcher, std::vector<std::string> addresses,
                ConnectionFactory factory);

  void StartConnectAll();

  auto GetNextNode() -> Node*;
  auto GetNodeCount() const -> size_t;
  auto GetAvailableCount() const -> size_t;

private:
  event::DispatcherSharedPtr dispatcher_;
  ConnectionFactory factory_;
  std::vector<std::unique_ptr<Node>> nodes_;
  size_t next_node_index_{0};
};

} // namespace carrot::io
