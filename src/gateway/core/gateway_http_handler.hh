#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gateway/core/result_receiver_storage.hh"
#include "common/core/io/connection.hh"
#include "common/core/io/llhttp_parser.hh"
#include "common/task/task.pb.h"
#include "strij/extensions/scheduler.hh"

namespace strij::gateway {

// Request header prefix that the gateway forwards into task parameters. The
// prefix is stripped and the key lowercased (HTTP header names are
// case-insensitive; llhttp preserves the wire case).
inline constexpr std::string_view kStrijHeaderPrefix = "x-strij-";

// Parses a task type from a request path of the form "/tasks/{type}".
// Returns std::nullopt if the path does not start with "/tasks/".
// Any query string (starting at '?') is stripped. The returned view may be
// empty when the path is exactly "/tasks/" (no type segment).
auto ParseTaskType(std::string_view path) -> std::optional<std::string_view>;

// Forwards headers matching the x-strij- prefix into task.parameters: the
// prefix is stripped, the key lowercased, and the value stored as-is. Pure and
// deterministic; other headers are ignored.
void PopulateParametersFromHeaders(strij::task::Task& task,
                                   const std::vector<std::pair<std::string, std::string>>& headers);

class GatewayHttpHandler final {
public:
  GatewayHttpHandler(ResultReceiverStorage& storage,
                     std::function<ResultReceiverPtr(io::Connection& conn)>&& make_receiver,
                     extensions::Scheduler& scheduler)
      : storage_{storage}, make_receiver_{std::move(make_receiver)}, scheduler_{scheduler} {}

  ~GatewayHttpHandler() = default;

  GatewayHttpHandler(const GatewayHttpHandler&) = delete;
  auto operator=(const GatewayHttpHandler&) -> GatewayHttpHandler& = delete;
  GatewayHttpHandler(GatewayHttpHandler&&) noexcept = delete;
  auto operator=(GatewayHttpHandler&&) noexcept -> GatewayHttpHandler& = delete;

  void HandleMessage(const io::HttpRequest& request, io::Connection& conn);

private:
  // TODO: storage_ is only needed to register a on-disconnect callback. Can we get rid of storage_
  // here by moving the callback registration to schedulers?
  ResultReceiverStorage& storage_;
  std::function<ResultReceiverPtr(io::Connection& conn)> make_receiver_;
  extensions::Scheduler& scheduler_;
};

} // namespace strij::gateway