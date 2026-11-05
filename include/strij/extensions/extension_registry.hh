#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace strij::extensions {

template <typename FactoryInterface> class Registry {
public:
  static auto instance() -> Registry& {
    static Registry registry;
    return registry;
  }

  void RegisterFactory(const std::string& name, std::unique_ptr<FactoryInterface> factory) {
    factories_[name] = std::move(factory);
  }

  // Takes ownership of the factory.
  void RegisterFactory(const std::string& name, FactoryInterface* factory) {
    RegisterFactory(name, std::unique_ptr<FactoryInterface>(factory));
  }

  auto GetFactory(const std::string& name) const -> FactoryInterface* {
    auto iter = factories_.find(name);
    return iter != factories_.end() ? iter->second.get() : nullptr;
  }

  auto GetRegisteredNames() const -> std::vector<std::string> {
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (const auto& [name, _] : factories_) {
      names.push_back(name);
    }
    return names;
  }

private:
  Registry() = default;
  std::unordered_map<std::string, std::unique_ptr<FactoryInterface>> factories_;
};

} // namespace strij::extensions

// Register a factory using static initialization.
// Use when FactoryClass is a simple name (not namespace-qualified).
// The macro uses ## to concatenate FactoryClass into the variable name,
// which fails if FactoryClass contains "::".
//
// Usage: put this at namespace scope in your .cc file.
//   REGISTER_FACTORY(MyFactory, MyInterface)
//
#define REGISTER_FACTORY(FactoryClass, FactoryInterface)                                           \
  namespace {                                                                                      \
  static void do_register_##FactoryClass() {                                                       \
    ::strij::extensions::Registry<FactoryInterface>::instance().RegisterFactory(                   \
        FactoryClass().Name(), std::make_unique<FactoryClass>());                                  \
  }                                                                                                \
  static const bool registered_##FactoryClass = (do_register_##FactoryClass(), true);              \
  }

// Register a factory when FactoryClass is namespace-qualified (contains "::").
// The ## operator cannot concatenate "::" into an identifier, so we need a
// separate RegistrarName parameter for the static variable name.
//
// Usage: pass a unique simple name as RegistrarName.
//   REGISTER_FACTORY_FULLY_QUALIFIED(
//       strij::extensions::node_discovery::StaticNodeDiscoveryFactory,
//       strij::extensions::NodeDiscoveryFactory,
//       static_node_discovery_registrar)
//
#define REGISTER_FACTORY_FULLY_QUALIFIED(FactoryClass, FactoryInterface, RegistrarName)            \
  namespace {                                                                                      \
  static void do_register_##RegistrarName() {                                                      \
    ::strij::extensions::Registry<FactoryInterface>::instance().RegisterFactory(                   \
        FactoryClass().Name(), std::make_unique<FactoryClass>());                                  \
  }                                                                                                \
  static const bool registered_##RegistrarName = (do_register_##RegistrarName(), true);            \
  }
