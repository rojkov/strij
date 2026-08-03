#include "core/gateway/node_directory.hh"

#include <algorithm>

namespace strij::gateway {

NodeDirectory::NodeDirectory(event::DispatcherSharedPtr dispatcher,
                             std::vector<std::string> addresses,
                             strij::io::ConnectionFactory factory)
    : dispatcher_{std::move(dispatcher)}, factory_{std::move(factory)} {
  nodes_.reserve(addresses.size());
  for (auto& addr : addresses) {
    nodes_.push_back(std::make_unique<Node>(std::move(addr), dispatcher_, factory_));
  }
}

void NodeDirectory::StartConnectAll() {
  for (auto& node : nodes_) {
    node->StartConnect();
  }
}

auto NodeDirectory::GetNextNode() -> Node* {
  if (nodes_.empty()) {
    return nullptr;
  }

  for (size_t i = 0; i < nodes_.size(); ++i) {
    size_t idx = (next_node_index_ + i) % nodes_.size();
    if (nodes_[idx]->IsAvailable()) {
      next_node_index_ = (idx + 1) % nodes_.size();
      return nodes_[idx].get();
    }
  }
  return nullptr;
}

auto NodeDirectory::GetNodeCount() const -> size_t { return nodes_.size(); }

auto NodeDirectory::GetAvailableCount() const -> size_t {
  return static_cast<size_t>(
      std::count_if(nodes_.begin(), nodes_.end(),
                    [](const auto& node) { return node->IsAvailable(); }));
}

} // namespace strij::gateway
