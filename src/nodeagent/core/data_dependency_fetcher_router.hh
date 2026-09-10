#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "strij/event/command_handler.hh"
#include "strij/event/dispatcher.hh"
#include "strij/extensions/data_dependency_fetcher.hh"
#include "strij/extensions/factory_context.hh"

namespace strij::nodeagent {

// Instantiates the configured data dependency fetchers: for each entry in
// `configs`, looks up the named factory in
// Registry<DataDependencyFetcherFactory>, unpacks its typed_config, and calls
// Create(). Returns InvalidArgumentError when a name is not registered (or its
// typed_config does not unpack). An empty list is valid and yields an empty
// vector (node starts without prefetch capability; probe deps never gate
// readiness). Scheme-ownership validation happens when the returned fetchers
// are handed to DataDependencyFetcherRouter::Build.
auto BuildDataDependencyFetchers(
    const ::google::protobuf::RepeatedPtrField<config::ExtensionConfig>& configs,
    extensions::NodeagentFactoryContext& context)
    -> absl::StatusOr<std::vector<extensions::DataDependencyFetcherPtr>>;

// Routes task data dependencies to the fetcher owning each DataRef's `source`
// scheme. Built once at nodeagent startup from the configured fetchers' union
// of HandledSourceTypes(); scheme ownership is disjoint (two fetchers claiming
// the same scheme is a startup error).
//
// The router also provides the stable task-id storage required by the
// DEP_COMPLETED command contract: args_ must point to a std::string that
// outlives command delivery, so FetchAll keeps a per-task-id token alive for
// the process lifetime. Fetchers may take the address of the task_id reference
// they receive and use it as args_ on completion.
//
// Refs whose scheme has no registered fetcher are skipped in FetchAll (logged)
// and never gate readiness in AllCached: a node without a fetcher for a scheme
// still runs the task, whose handler fetches data on demand. This keeps probes
// from stalling on nodes with a partial fetcher set.
class DataDependencyFetcherRouter {
public:
  // Constructs the routing table from `fetchers`, taking ownership of them.
  // Returns InvalidArgumentError if two fetchers claim the same source scheme
  // (startup validation, mirroring frame-type ownership).
  static auto Build(nodeagent::ObjectCache& cache,
                    std::vector<extensions::DataDependencyFetcherPtr> fetchers)
      -> absl::StatusOr<std::shared_ptr<DataDependencyFetcherRouter>>;

  DataDependencyFetcherRouter(const DataDependencyFetcherRouter&) = delete;
  auto operator=(const DataDependencyFetcherRouter&) -> DataDependencyFetcherRouter& = delete;
  DataDependencyFetcherRouter(DataDependencyFetcherRouter&&) = delete;
  auto operator=(DataDependencyFetcherRouter&&) -> DataDependencyFetcherRouter& = delete;
  ~DataDependencyFetcherRouter() = default;

  // Starts a fetch for every dep whose scheme has a registered fetcher.
  // Fire-and-forget; summaries to `destination` arrive via DEP_COMPLETED
  // commands on `dispatcher`. Fetches for refs already in the cache are
  // short-circuited by the owning fetcher (DEP_COMPLETED issued immediately).
  void FetchAll(const google::protobuf::RepeatedPtrField<task::DataRef>& deps,
                const std::string& task_id, event::Dispatcher& dispatcher,
                event::CommandHandler* destination);

  // Returns true iff every dep is either cached or carries a scheme with no
  // registered fetcher (such refs never gate readiness). Empty deps -> true.
  [[nodiscard]] auto AllCached(const google::protobuf::RepeatedPtrField<task::DataRef>& deps) const
      -> bool;

  // True when no fetcher is registered (NodeAgentConfig.data_dependency_fetchers empty):
  // FetchAll is a no-op and AllCached is trivially true.
  [[nodiscard]] auto empty() const -> bool;

private:
  DataDependencyFetcherRouter(
      nodeagent::ObjectCache& cache, std::vector<extensions::DataDependencyFetcherPtr> fetchers,
      std::unordered_map<std::string, extensions::DataDependencyFetcher*> by_source);

  nodeagent::ObjectCache& cache_;
  std::vector<extensions::DataDependencyFetcherPtr> fetchers_;
  std::unordered_map<std::string, extensions::DataDependencyFetcher*> by_source_;
  // Stable per-task-id strings backing DEP_COMPLETED args_. Token values are
  // equal to the originating task id and survive for the process lifetime, so a
  // command delivered after the scheduler moved/removed its own entries still
  // carries a readable, correctly-valued id.
  std::unordered_map<std::string, std::shared_ptr<std::string>> task_id_tokens_;
};

using DataDependencyFetcherRouterPtr = std::shared_ptr<DataDependencyFetcherRouter>;

} // namespace strij::nodeagent