#include "extensions/node_discovery/static/static_node_discovery.hh"

#include <stdexcept>

#include "extensions/node_discovery/static/static_node_discovery.pb.h"
#include "google/protobuf/any.pb.h"

namespace carrot::extensions::node_discovery {

StaticNodeDiscovery::StaticNodeDiscovery(std::vector<std::string> addresses)
    : addresses_(std::move(addresses)) {}

void StaticNodeDiscovery::Start(DiscoveryCallback callback) {
  std::vector<NodeInfo> nodes;
  nodes.reserve(addresses_.size());
  for (const auto& addr : addresses_) {
    nodes.push_back(NodeInfo{addr});
  }
  callback(std::move(nodes));
}

void StaticNodeDiscovery::Stop() {}

auto StaticNodeDiscoveryFactory::Name() const -> std::string { return "static"; }

auto StaticNodeDiscoveryFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<carrot::config::StaticNodeDiscoveryConfig>();
}

auto StaticNodeDiscoveryFactory::Create(const ::google::protobuf::Message& config,
                                        FactoryContext& /*context*/)
    -> std::unique_ptr<NodeDiscovery> {
  const auto& typed = dynamic_cast<const carrot::config::StaticNodeDiscoveryConfig&>(config);

  if (typed.addresses().empty()) {
    throw std::runtime_error("StaticNodeDiscoveryConfig: addresses must not be empty");
  }

  std::vector<std::string> addresses;
  for (const auto& addr : typed.addresses()) {
    addresses.push_back(addr);
  }

  return std::make_unique<StaticNodeDiscovery>(std::move(addresses));
}

} // namespace carrot::extensions::node_discovery

REGISTER_FACTORY_FULLY_QUALIFIED(carrot::extensions::node_discovery::StaticNodeDiscoveryFactory,
                                 carrot::extensions::NodeDiscoveryFactory,
                                 static_node_discovery_registrar)
