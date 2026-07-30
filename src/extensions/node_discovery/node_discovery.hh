#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "google/protobuf/message.h"

#include "carrot/common/pure.hh"
#include "core/extensions/factory_context.hh"
#include "core/extensions/extension_registry.hh"

namespace carrot::extensions {

struct NodeInfo {
  std::string address;
};

class NodeDiscovery {
public:
  using DiscoveryCallback = std::function<void(std::vector<NodeInfo>)>;

  virtual ~NodeDiscovery() = default;
  virtual void Start(DiscoveryCallback callback) PURE;
  virtual void Stop() PURE;
};

class NodeDiscoveryFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  virtual ~NodeDiscoveryFactory() = default;
  virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<NodeDiscovery> PURE;
};

} // namespace carrot::extensions
