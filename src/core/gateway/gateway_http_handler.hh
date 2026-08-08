#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/llhttp_parser.hh"
#include "core/task/task.pb.h"

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
void PopulateParametersFromHeaders(
    strij::task::Task& task,
    const std::vector<std::pair<std::string, std::string>>& headers);

class GatewayHttpHandler {
public:
  GatewayHttpHandler(NodeDirectory& node_directory, ResultReceiverStorage& storage,
                     std::unique_ptr<ResultReceiver> (*make_receiver)(strij::io::Connection& conn))
      : node_directory_{node_directory}, storage_{storage}, make_receiver_{make_receiver} {}

  void HandleMessage(strij::io::HttpRequest request, strij::io::Connection& conn);

private:
  NodeDirectory& node_directory_;
  ResultReceiverStorage& storage_;
  std::unique_ptr<ResultReceiver> (*make_receiver_)(strij::io::Connection& conn);
};

} // namespace strij::gateway
