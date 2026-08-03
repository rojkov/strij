## Design Influence

This extension mechanism is heavily influenced by the [Envoy proxy](https://www.envoyproxy.io/) extension architecture: compile-time factory registration via static macros, typed registries per extension category, and `google.protobuf.Any` for config serialization.

## Architecture

```
src/core/extensions/                  src/extensions/node_discovery/
├── extension_registry.hh             ├── node_discovery.hh
│   Registry<T> template              │   NodeDiscovery interface
│   REGISTER_FACTORY macro            │   NodeDiscoveryFactory
├── factory_context.hh                ├── static/
│   FactoryContext interface           │   ├── static_node_discovery.hh/.cc
│   GatewayFactoryContext              │   ├── static_node_discovery.proto
├── BUILD.bazel                       │   └── BUILD.bazel
│                                     └── BUILD.bazel

src/core/config/proto/
├── extensions.proto                  ExtensionConfig message
├── gateway.proto                     + node_discovery field
└── BUILD.bazel
```

## Registry Template

```cpp
// src/core/extensions/extension_registry.hh
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace strij::extensions {

template <typename FactoryInterface>
class Registry {
public:
  static Registry& instance() {
    static Registry registry;
    return registry;
  }

  void registerFactory(const std::string& name, FactoryInterface* factory) {
    factories_[name] = factory;
  }

  auto getFactory(const std::string& name) const -> FactoryInterface* {
    auto it = factories_.find(name);
    return it != factories_.end() ? it->second : nullptr;
  }

  auto getRegisteredNames() const -> std::vector<std::string> {
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (const auto& [name, _] : factories_) {
      names.push_back(name);
    }
    return names;
  }

private:
  Registry() = default;
  std::unordered_map<std::string, FactoryInterface*> factories_;
};

} // namespace strij::extensions
```

## Registration Macro

```cpp
// At the bottom of extension_registry.hh
#define REGISTER_FACTORY(FactoryClass, FactoryInterface)                          \
  namespace {                                                                     \
  static struct FactoryClass##_Registrar {                                        \
    FactoryClass##_Registrar() {                                                  \
      ::strij::extensions::Registry<FactoryInterface>::instance().registerFactory( \
          FactoryClass().name(), new FactoryClass());                             \
    }                                                                             \
  } registrar_##FactoryClass;                                                     \
  }
```

Static initialization registers each factory into the singleton registry at program startup. No dynamic loading needed — the linker pulls in the `.o` files from Bazel deps.

## FactoryContext

```cpp
// src/core/extensions/factory_context.hh
#pragma once

#include "include/strij/event/dispatcher.hh"
#include "src/core/logging/logger.hh"

namespace strij::extensions {

class FactoryContext {
public:
  virtual ~FactoryContext() = default;
  virtual auto dispatcher() -> event::Dispatcher& = 0;
  virtual auto logger() -> logging::Logger& = 0;
};

class GatewayFactoryContext : public FactoryContext {
public:
  GatewayFactoryContext(event::DispatcherSharedPtr dispatcher);
  auto dispatcher() -> event::Dispatcher& override;
  auto logger() -> logging::Logger& override;

private:
  event::DispatcherSharedPtr dispatcher_;
};

} // namespace strij::extensions
```

## ExtensionConfig Protobuf

```protobuf
// src/core/config/proto/extensions.proto
syntax = "proto3";
package strij.config;

import "google/protobuf/any.proto";

message ExtensionConfig {
  string name = 1;
  google.protobuf.Any typed_config = 2;
}
```

## GatewayConfig Changes

```protobuf
// src/core/config/proto/gateway.proto (additions)
import "extensions.proto";

message GatewayConfig {
  HttpListener http_listener = 1;
  repeated NodeConnection node_connections = 2;
  Logging logging = 3;
  ExtensionConfig node_discovery = 4;  // NEW
}
```

## NodeDiscovery Interface

```cpp
// src/extensions/node_discovery/node_discovery.hh
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/core/extensions/factory_context.hh"
#include "src/core/extensions/extension_registry.hh"

namespace strij::extensions {

struct NodeInfo {
  std::string address;
};

class NodeDiscovery {
public:
  using DiscoveryCallback = std::function<void(std::vector<NodeInfo>)>;

  virtual ~NodeDiscovery() = default;
  virtual void start(DiscoveryCallback callback) = 0;
  virtual void stop() = 0;
};

class NodeDiscoveryFactory {
public:
  virtual ~NodeDiscoveryFactory() = default;
  virtual auto name() const -> std::string = 0;
  virtual auto create(const ::google::protobuf::Message& config,
                      FactoryContext& context) -> std::unique_ptr<NodeDiscovery> = 0;
};

} // namespace strij::extensions
```

## StaticNodeDiscovery (built-in)

```cpp
// src/extensions/node_discovery/static/static_node_discovery.hh
#pragma once

#include "src/extensions/node_discovery/node_discovery.hh"

namespace strij::extensions::node_discovery {

class StaticNodeDiscovery : public NodeDiscovery {
public:
  explicit StaticNodeDiscovery(std::vector<std::string> addresses);
  void start(DiscoveryCallback callback) override;
  void stop() override;

private:
  std::vector<std::string> addresses_;
};

class StaticNodeDiscoveryFactory : public NodeDiscoveryFactory {
public:
  auto name() const -> std::string override { return "static"; }
  auto create(const ::google::protobuf::Message& config,
              FactoryContext& context) -> std::unique_ptr<NodeDiscovery> override;
};

} // namespace strij::extensions::node_discovery
```

The factory unpacks `StaticNodeDiscoveryConfig` from the `google.protobuf.Any`, validates that `addresses` is non-empty, and returns a `StaticNodeDiscovery`. On `start()`, it immediately invokes the callback with the configured addresses.

## Gateway Wiring

```cpp
// src/exe/gateway/gateway.cc (key changes)
#include "src/core/extensions/extension_registry.hh"
#include "src/core/extensions/factory_context.hh"
#include "src/extensions/node_discovery/node_discovery.hh"

// After config load:
auto dispatcher = std::make_shared<DispatcherImpl>();
GatewayFactoryContext factory_context(dispatcher);

std::unique_ptr<extensions::NodeDiscovery> node_discovery;

if (config.has_node_discovery()) {
  const auto& ext = config.node_discovery();
  auto* factory = extensions::Registry<extensions::NodeDiscoveryFactory>::instance()
                      .getFactory(ext.name());
  if (factory) {
    google::protobuf::MessagePtr unpacked = factory->createEmptyConfigProto();
    ext.typed_config().UnpackTo(unpacked.get());
    node_discovery = factory->create(*unpacked, factory_context);
  }
}

// Fallback to legacy node_connections
if (!node_discovery && !config.node_connections().empty()) {
  std::vector<std::string> addrs;
  for (const auto& n : config.node_connections()) {
    addrs.push_back(n.address());
  }
  node_discovery = std::make_unique<StaticNodeDiscovery>(std::move(addrs));
}

// Use node_discovery->start() instead of NodeDirectory constructor
```

## Bazel Structure

```
src/core/extensions/BUILD.bazel:
  extension_registry_lib   — header-only, no deps
  factory_context_lib      — depends on dispatcher_interface, log_lib

src/extensions/node_discovery/BUILD.bazel:
  node_discovery_interface — header-only, depends on extension_registry_lib, factory_context_lib

src/extensions/node_discovery/static/BUILD.bazel:
  static_node_discovery_lib — .cc + .hh, depends on node_discovery_interface, static_node_discovery_proto
  static_node_discovery_proto — proto_library
```

## User Extension Example

A user creates `src/extensions/node_discovery/etcd/` with their implementation, adds a `BUILD.bazel`, and adds the target to `gateway/BUILD.bazel` deps. The `REGISTER_FACTORY` macro in their `.cc` file auto-registers at startup. No gateway code changes needed.