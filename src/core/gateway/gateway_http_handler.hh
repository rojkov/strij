#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/llhttp_parser.hh"

namespace carrot::gateway {

// Parses a task type from a request path of the form "/tasks/{type}".
// Returns std::nullopt if the path does not start with "/tasks/".
// Any query string (starting at '?') is stripped. The returned view may be
// empty when the path is exactly "/tasks/" (no type segment).
auto ParseTaskType(std::string_view path) -> std::optional<std::string_view>;

class GatewayHttpHandler {
public:
  GatewayHttpHandler(NodeDirectory& node_directory, ResultReceiverStorage& storage,
                     std::unique_ptr<ResultReceiver> (*make_receiver)(carrot::io::Connection& conn))
      : node_directory_{node_directory}, storage_{storage}, make_receiver_{make_receiver} {}

  void HandleMessage(carrot::io::HttpRequest request, carrot::io::Connection& conn);

private:
  NodeDirectory& node_directory_;
  ResultReceiverStorage& storage_;
  std::unique_ptr<ResultReceiver> (*make_receiver_)(carrot::io::Connection& conn);
};

} // namespace carrot::gateway
