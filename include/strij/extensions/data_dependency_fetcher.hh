#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "common/task/task.pb.h"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"
#include "strij/event/command_handler.hh"
#include "strij/event/dispatcher.hh"
#include "strij/extensions/factory_context.hh"

namespace strij::extensions {

// A data dependency fetcher extension: owns fetching for a disjoint set of
// `source` schemes (e.g. {"http"}, {"s3"}). Instances are created at nodeagent
// startup from NodeAgentConfig.data_dependency_fetchers and owned by the
// DataDependencyFetcherRouter, which dispatches each task dep to the fetcher
// that claims its source scheme.
//
// Contract: Fetch() is fire-and-forget and must not block the event loop. On
// completion the fetcher Populates the shared ObjectCache (reached via its
// NodeagentFactoryContext at construction) and then submits a DEP_COMPLETED
// command (destination_ = `destination`, args_ = a stable std::string* holding
// `task_id` owned by the receiver) through `dispatcher`; if the ref is already
// cached the fetcher must short-circuit and submit DEP_COMPLETED immediately.
class DataDependencyFetcher {
public:
  DataDependencyFetcher() = default;
  virtual ~DataDependencyFetcher() = default;

  DataDependencyFetcher(const DataDependencyFetcher&) = delete;
  auto operator=(const DataDependencyFetcher&) -> DataDependencyFetcher& = delete;
  DataDependencyFetcher(DataDependencyFetcher&&) noexcept = delete;
  auto operator=(DataDependencyFetcher&&) noexcept -> DataDependencyFetcher& = delete;

  // Starts fetching `ref` on behalf of `task_id`. `destination` is the
  // CommandHandler to notify via DEP_COMPLETED when the fetch finishes.
  virtual void Fetch(const task::DataRef& ref, const std::string& task_id,
                     event::Dispatcher& dispatcher,
                     event::CommandHandler* destination) PURE;

  // The source schemes this fetcher handles (e.g. {"http"}). Schemes are
  // ownership-disjoint across fetchers on a node.
  [[nodiscard]] virtual auto HandledSourceTypes() const -> std::span<const std::string_view> PURE;
};

using DataDependencyFetcherPtr = std::unique_ptr<DataDependencyFetcher>;

} // namespace strij::extensions

namespace strij::nodeagent {

// Nodeagent-side data dependency fetcher factory, registered in
// extensions::Registry<nodeagent::DataDependencyFetcherFactory>.
class DataDependencyFetcherFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  DataDependencyFetcherFactory() = default;
  virtual ~DataDependencyFetcherFactory() = default;

  DataDependencyFetcherFactory(const DataDependencyFetcherFactory&) = delete;
  auto operator=(const DataDependencyFetcherFactory&) -> DataDependencyFetcherFactory& = delete;
  DataDependencyFetcherFactory(DataDependencyFetcherFactory&&) noexcept = delete;
  auto operator=(DataDependencyFetcherFactory&&) noexcept -> DataDependencyFetcherFactory& = delete;

  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config,
                      extensions::NodeagentFactoryContext& context)
      -> extensions::DataDependencyFetcherPtr PURE;
};

} // namespace strij::nodeagent