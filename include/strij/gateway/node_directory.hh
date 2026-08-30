#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "strij/common/pure.hh"

namespace strij::gateway {

class Node;

// Pure-abstract contract for the gateway's node pool, consumed by gateway
// scheduler extensions. Bundled extension authors implement against this
// interface instead of the concrete NodeDirectoryImpl, so the gateway can swap
// implementations without breaking the seam.
class NodeDirectory {
public:
  NodeDirectory() = default;
  virtual ~NodeDirectory() = default;

  NodeDirectory(const NodeDirectory&) = delete;
  auto operator=(const NodeDirectory&) -> NodeDirectory& = delete;
  NodeDirectory(NodeDirectory&&) noexcept = delete;
  auto operator=(NodeDirectory&&) noexcept -> NodeDirectory& = delete;

  // Adds a node and starts connecting it. A no-op for an existing node_id.
  virtual void AddNode(const std::string& node_id, const std::string& address) PURE;
  // Disconnects and drops the node. A no-op for an unknown node_id.
  virtual void RemoveNode(const std::string& node_id) PURE;

  virtual auto GetNode(const std::string& node_id) -> Node* PURE;
  virtual auto GetNextNode() -> Node* PURE;
  // Returns connected nodes whose advertisement lists `protocol` in
  // scheduling_protocols. A node that is connected but has not advertised yet
  // is treated as eligible (the advertisement is the first frame on the
  // connection, so this only covers the handshake window).
  virtual auto GetCandidates(std::string_view protocol) -> std::vector<Node*> PURE;
  virtual auto GetNodeCount() const -> size_t PURE;
  virtual auto GetAvailableCount() const -> size_t PURE;
};

} // namespace strij::gateway
