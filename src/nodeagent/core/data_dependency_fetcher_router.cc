#include "nodeagent/core/data_dependency_fetcher_router.hh"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "common/core/logging/log.hh"
#include "google/protobuf/any.pb.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::nodeagent {

auto BuildDataDependencyFetchers(
    const ::google::protobuf::RepeatedPtrField<config::ExtensionConfig>& configs,
    extensions::NodeagentFactoryContext& context)
    -> absl::StatusOr<std::vector<extensions::DataDependencyFetcherPtr>> {
  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  fetchers.reserve(static_cast<size_t>(configs.size()));
  for (const auto& ext : configs) {
    auto* registry =
        &extensions::Registry<nodeagent::DataDependencyFetcherFactory>::instance();
    auto* factory = registry->GetFactory(ext.name());
    if (factory == nullptr) {
      const auto names = registry->GetRegisteredNames();
      return absl::InvalidArgumentError(absl::StrCat(
          "Data dependency fetcher '", ext.name(), "' not found. Registered: ",
          names.empty() ? "(none)" : absl::StrJoin(names, ", ")));
    }

    ::google::protobuf::Any unpacked;
    unpacked.CopyFrom(ext.typed_config());
    auto config_msg = factory->CreateEmptyConfigProto();
    // Tolerate an entry without a packed typed_config: factory defaults apply.
    if (!unpacked.type_url().empty() && !unpacked.UnpackTo(config_msg.get())) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to unpack typed_config for data dependency fetcher '", ext.name(),
          "': unknown type '", unpacked.type_url(), "'"));
    }

    auto fetcher = factory->Create(*config_msg, context);
    if (fetcher == nullptr) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Data dependency fetcher factory '", ext.name(),
          "' rejected the configuration"));
    }
    fetchers.push_back(std::move(fetcher));
  }
  return fetchers;
}

auto DataDependencyFetcherRouter::Build(
    nodeagent::ObjectCache& cache, std::vector<extensions::DataDependencyFetcherPtr> fetchers)
    -> absl::StatusOr<std::shared_ptr<DataDependencyFetcherRouter>> {
  std::unordered_map<std::string, extensions::DataDependencyFetcher*> by_source;
  for (const auto& fetcher : fetchers) {
    for (const std::string_view scheme : fetcher->HandledSourceTypes()) {
      auto [iter, inserted] = by_source.emplace(std::string(scheme), fetcher.get());
      if (!inserted) {
        return absl::InvalidArgumentError(
            absl::StrCat("Duplicate source scheme '", scheme,
                         "' claimed by more than one data dependency fetcher"));
      }
    }
  }
  return std::shared_ptr<DataDependencyFetcherRouter>(
      new DataDependencyFetcherRouter(cache, std::move(fetchers), std::move(by_source)));
}

DataDependencyFetcherRouter::DataDependencyFetcherRouter(
    nodeagent::ObjectCache& cache, std::vector<extensions::DataDependencyFetcherPtr> fetchers,
    std::unordered_map<std::string, extensions::DataDependencyFetcher*> by_source)
    : cache_{cache}, fetchers_{std::move(fetchers)}, by_source_{std::move(by_source)} {}

void DataDependencyFetcherRouter::FetchAll(
    const google::protobuf::RepeatedPtrField<task::DataRef>& deps, const std::string& task_id,
    event::Dispatcher& dispatcher, event::CommandHandler* destination) {
  // A stable token for the task id: the DEP_COMPLETED args_ pointer must
  // outlive command delivery, and the scheduler's own per-task entries may move
  // or be removed before the fetch completes.
  auto [token_iter, token_inserted] =
      task_id_tokens_.try_emplace(task_id, std::make_shared<std::string>(task_id));
  const std::string& stable_task_id = *token_iter->second;

  for (const auto& ref : deps) {
    auto iter = by_source_.find(ref.source());
    if (iter == by_source_.end()) {
      LOG_WARNING("No data dependency fetcher for source '{}' (task {}); ref {} skipped",
                  ref.source(), task_id, ref.key());
      continue;
    }
    iter->second->Fetch(ref, stable_task_id, dispatcher, destination);
  }
}

auto DataDependencyFetcherRouter::AllCached(
    const google::protobuf::RepeatedPtrField<task::DataRef>& deps) const -> bool {
  for (const auto& ref : deps) {
    // Refs for schemes we cannot fetch never gate readiness: the task handler
    // fetches on demand, so a partial fetcher set must not stall the probe.
    if (by_source_.contains(ref.source()) && !cache_.IsCached(ref)) {
      return false;
    }
  }
  return true;
}

auto DataDependencyFetcherRouter::empty() const -> bool { return fetchers_.empty(); }

} // namespace strij::nodeagent