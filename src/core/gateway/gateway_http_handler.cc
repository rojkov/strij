#include "core/gateway/gateway_http_handler.hh"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core/io/connection.hh"
#include "core/io/llhttp_parser.hh"
#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/task/task.pb.h"
#include "core/utils/task_id.hh"

namespace strij::gateway {

auto ParseTaskType(std::string_view path) -> std::optional<std::string_view> {
  constexpr std::string_view kTasksPrefix = "/tasks/";
  if (!path.starts_with(kTasksPrefix)) {
    return std::nullopt;
  }

  auto type = path.substr(kTasksPrefix.size());
  if (const auto query_pos = type.find('?'); query_pos != std::string_view::npos) {
    type = type.substr(0, query_pos);
  }
  return type;
}

void PopulateParametersFromHeaders(
    task::Task& task, const std::vector<std::pair<std::string, std::string>>& headers) {
  auto* parameters = task.mutable_parameters();
  for (const auto& [name, value] : headers) {
    std::string lower_name = name;
    std::ranges::transform(lower_name, lower_name.begin(),
                           [](unsigned char chr) -> unsigned char { return std::tolower(chr); });
    if (!lower_name.starts_with(kStrijHeaderPrefix)) {
      continue;
    }
    auto key = lower_name.substr(kStrijHeaderPrefix.size());
    if (key.empty()) {
      continue;
    }
    (*parameters)[key] = value;
  }
}

namespace {

constexpr int kStatusBadRequest = 400;
constexpr int kStatusNotFound = 404;
constexpr int kStatusInternalServerError = 500;
constexpr int kStatusServiceUnavailable = 503;

void writeErrorResponse(io::Connection& conn, int status, std::string_view reason) {
  auto response = std::format("HTTP/1.1 {} {}\r\nContent-Length: 0\r\nContent-Type: "
                              "text/plain\r\nConnection: close\r\n\r\n",
                              status, reason);
  auto response_bytes = std::as_bytes(std::span(response.data(), response.size()));
  conn.Write(response_bytes);
}

} // namespace

void GatewayHttpHandler::HandleMessage(const io::HttpRequest& request, io::Connection& conn) {
  auto task_type = ParseTaskType(request.path);
  if (!task_type.has_value()) {
    writeErrorResponse(conn, kStatusNotFound, "Not Found");
    return;
  }
  if (task_type->empty()) {
    writeErrorResponse(conn, kStatusBadRequest, "Bad Request");
    return;
  }

  auto task_id = utils::GenerateTaskId();
  task::Task task;
  task.set_id(task_id);
  task.set_type(task_type->data(), task_type->size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  task.set_body(reinterpret_cast<const char*>(request.body.data()), request.body.size());
  PopulateParametersFromHeaders(task, request.headers);

  std::string serialized;
  if (!task.SerializeToString(&serialized)) {
    writeErrorResponse(conn, kStatusInternalServerError, "Internal Failure");
    return;
  }

  auto* node = node_directory_.GetNextNode();
  if (node == nullptr) {
    writeErrorResponse(conn, kStatusServiceUnavailable, "Service Unavailable");
    return;
  }

  auto* nodeagent_conn = node->GetConnection();

  auto receiver = make_receiver_(conn);
  storage_.put(task_id, std::move(receiver));

  auto frame =
      io::SerializeTlvFrame(io::TlvFrame::kTaskSubmission,
                            std::as_bytes(std::span(serialized.data(), serialized.size())));
  nodeagent_conn->Write(frame);

  LOG_DEBUG("Submitted task {} (type {}) to nodeagent", task_id, task_type.value());
}

} // namespace strij::gateway
