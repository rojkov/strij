#pragma once

#include <string>
#include <vector>

#include "extensions/node_discovery/node_discovery.hh"

namespace carrot::extensions::node_discovery {

class StaticNodeDiscovery : public NodeDiscovery {
public:
  explicit StaticNodeDiscovery(std::vector<std::string> addresses);

  void Start(DiscoveryCallback callback) override;
  void Stop() override;

private:
  std::vector<std::string> addresses_;
};

class StaticNodeDiscoveryFactory : public NodeDiscoveryFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<NodeDiscovery> override;
};

} // namespace carrot::extensions::node_discovery
