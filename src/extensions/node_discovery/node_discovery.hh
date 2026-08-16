#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/extensions/factory_context.hh"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"

namespace strij::extensions {

struct NodeInfo {
  std::string node_id_;
  std::string address_;
};

class NodeDiscovery {
public:
  using DiscoveryCallback = std::function<void(std::vector<NodeInfo>)>;

  NodeDiscovery() = default;
  virtual ~NodeDiscovery() = default;

  NodeDiscovery(const NodeDiscovery&) = delete;
  auto operator=(const NodeDiscovery&) -> NodeDiscovery& = delete;
  NodeDiscovery(NodeDiscovery&&) noexcept = delete;
  auto operator=(NodeDiscovery&&) noexcept -> NodeDiscovery& = delete;

  virtual void Start(DiscoveryCallback callback) PURE;
  virtual void Stop() PURE;
};

class NodeDiscoveryFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  NodeDiscoveryFactory() = default;
  virtual ~NodeDiscoveryFactory() = default;

  NodeDiscoveryFactory(const NodeDiscoveryFactory&) = delete;
  auto operator=(const NodeDiscoveryFactory&) -> NodeDiscoveryFactory& = delete;
  NodeDiscoveryFactory(NodeDiscoveryFactory&&) noexcept = delete;
  auto operator=(NodeDiscoveryFactory&&) noexcept -> NodeDiscoveryFactory& = delete;

  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<NodeDiscovery> PURE;
};

} // namespace strij::extensions
