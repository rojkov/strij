#include "core/gateway/node_directory.hh"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/logging/log.hh"

namespace strij::gateway {

NodeDirectory::NodeDirectory(event::DispatcherSharedPtr dispatcher, io::ConnectionFactory factory)
    : dispatcher_{std::move(dispatcher)}, factory_{std::move(factory)} {}

void NodeDirectory::AddNode(const std::string& node_id, const std::string& address) {
  auto node = std::make_unique<Node>(node_id, address, dispatcher_, factory_);
  Node* node_raw_ptr = node.get();
  nodes_.insert_or_assign(node_id, std::move(node));
  node_raw_ptr->StartConnect();
}

void NodeDirectory::RemoveNode(const std::string& node_id) {
  auto iter = nodes_.find(node_id);
  if (iter == nodes_.end()) {
    return;
  }

  iter->second->Disconnect();

  // Drop origin mappings that resolve to this node, so a future re-add of the
  // same placeholder identity creates a fresh record.
  for (auto origin_it = origin_to_canonical_.begin(); origin_it != origin_to_canonical_.end();) {
    if (origin_it->second == node_id) {
      origin_it = origin_to_canonical_.erase(origin_it);
    } else {
      ++origin_it;
    }
  }

  nodes_.erase(iter);
}

void NodeDirectory::Reconcile(const std::vector<extensions::NodeInfo>& snapshot) {
  std::set<std::string> present_ids;
  for (const auto& info : snapshot) {
    // Resolve placeholder identities that were rekeyed to a canonical id.
    auto origin = origin_to_canonical_.find(info.node_id_);
    const std::string& key = origin != origin_to_canonical_.end() ? origin->second : info.node_id_;
    present_ids.insert(key);

    auto iter = nodes_.find(key);
    if (iter == nodes_.end()) {
      AddNode(info.node_id_, info.address_);
    } else if (iter->second->GetAddress() != info.address_) {
      LOG_INFO("Node {} re-announced at new address {}; reconnecting", key, info.address_);
      iter->second->ReconnectTo(info.address_);
    }
  }

  for (auto it = nodes_.begin(); it != nodes_.end();) {
    if (present_ids.contains(it->first)) {
      ++it;
      continue;
    }

    LOG_INFO("Node {} no longer in discovery snapshot; removing", it->first);
    it->second->Disconnect();
    it = nodes_.erase(it);
  }
}

void NodeDirectory::RekeyNode(const std::string& from_id, const std::string& to_id) {
  if (from_id == to_id) {
    return;
  }

  auto iter = nodes_.find(from_id);
  if (iter == nodes_.end()) {
    LOG_WARNING("Cannot rekey unknown node '{}'", from_id);
    return;
  }

  if (nodes_.contains(to_id)) {
    LOG_WARNING("Cannot rekey '{}' to '{}': identity already in use", from_id, to_id);
    return;
  }

  // Chain origin mappings (e.g. a placeholder that itself was an origin).
  for (auto& [origin, canonical] : origin_to_canonical_) {
    if (canonical == from_id) {
      canonical = to_id;
    }
  }

  origin_to_canonical_[from_id] = to_id;
  iter->second->SetNodeId(to_id);
  nodes_.emplace(to_id, std::move(iter->second));
  nodes_.erase(iter);
}

auto NodeDirectory::GetNode(const std::string& node_id) -> Node* {
  auto iter = nodes_.find(node_id);
  return iter != nodes_.end() ? iter->second.get() : nullptr;
}

auto NodeDirectory::GetNextNode() -> Node* {
  if (nodes_.empty()) {
    return nullptr;
  }

  const size_t total = nodes_.size();
  for (size_t i = 0; i < total; ++i) {
    const size_t idx = (next_node_index_ + i) % total;
    auto iter = nodes_.begin();
    std::advance(iter, static_cast<ssize_t>(idx));

    if (iter->second->IsAvailable()) {
      next_node_index_ = (idx + 1) % total;
      return iter->second.get();
    }
  }

  return nullptr;
}

auto NodeDirectory::GetCandidates(std::string_view protocol) -> std::vector<Node*> {
  std::vector<Node*> candidates;
  candidates.reserve(nodes_.size());
  for (const auto& [node_id, node] : nodes_) {
    if (!node->IsAvailable()) {
      continue;
    }

    const auto* capabilities = node->GetCapabilities();
    if (capabilities == nullptr) {
      // Handshake window: the node is connected but has not advertised yet.
      candidates.push_back(node.get());
      continue;
    }

    const bool advertises_protocol = std::any_of(
        capabilities->scheduling_protocols().begin(), capabilities->scheduling_protocols().end(),
        [protocol](const auto& entry) -> bool { return entry.name() == protocol; });
    if (advertises_protocol) {
      candidates.push_back(node.get());
    }
  }

  return candidates;
}

auto NodeDirectory::GetNodeCount() const -> size_t { return nodes_.size(); }

auto NodeDirectory::GetAvailableCount() const -> size_t {
  return static_cast<size_t>(std::count_if(
      nodes_.begin(), nodes_.end(), [](const auto& entry) -> bool { return entry.second->IsAvailable(); }));
}

} // namespace strij::gateway
