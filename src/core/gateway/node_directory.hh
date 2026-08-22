#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/gateway/node.hh"
#include "core/io/connection.hh"
#include "extensions/node_discovery/node_discovery.hh"
#include "strij/event/dispatcher.hh"

namespace strij::gateway {

class ResultReceiverStorage;

// Owns the pool of Node instances, keyed by the stable node_id. Membership is
// driven by repeated discovery snapshots via Reconcile(); the gateway never
// invents nodes.
class NodeDirectory {
public:
  NodeDirectory(event::DispatcherSharedPtr dispatcher, io::ConnectionFactory factory,
                ResultReceiverStorage& storage);

  // Adds a node and starts connecting it. A no-op for an existing node_id.
  void AddNode(const std::string& node_id, const std::string& address);
  // Disconnects and drops the node. A no-op for an unknown node_id.
  void RemoveNode(const std::string& node_id);
  // Diffs a full discovery snapshot against current membership: adds new
  // identities, removes gone ones, and reconnects existing ones whose address
  // changed. Snapshot entries whose node_id was previously rekeyed (see
  // RekeyNode) are matched by their canonical identity.
  void Reconcile(const std::vector<extensions::NodeInfo>& snapshot);
  // Rekeys the record `from_id` to the canonical `to_id` learned from the
  // node's advertisement. A no-op when `to_id` is already taken. Subsequent
  // reconciliation snapshots referring to `from_id` resolve to `to_id`.
  void RekeyNode(const std::string& from_id, const std::string& to_id);

  auto GetNode(const std::string& node_id) -> Node*;
  auto GetNextNode() -> Node*;
  // Returns connected nodes whose advertisement lists `protocol` in
  // scheduling_protocols. A node that is connected but has not advertised yet
  // is treated as eligible (the advertisement is the first frame on the
  // connection, so this only covers the handshake window).
  auto GetCandidates(std::string_view protocol) -> std::vector<Node*>;
  auto GetNodeCount() const -> size_t;
  auto GetAvailableCount() const -> size_t;

private:
  event::DispatcherSharedPtr dispatcher_;
  io::ConnectionFactory factory_;
  ResultReceiverStorage& storage_;
  // std::map keeps a deterministic iteration order for round-robin selection.
  std::map<std::string, NodePtr> nodes_;
  // Maps discovery-derived (placeholder) node identities to the canonical
  // identity learned from a node's advertisement.
  std::unordered_map<std::string, std::string> origin_to_canonical_;
  size_t next_node_index_{0};
};

} // namespace strij::gateway
