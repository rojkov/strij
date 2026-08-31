#include "gateway/core/gateway_http_handler.hh"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "gateway/core/requirements_resolver.hh"
#include "common/core/io/connection.hh"
#include "common/core/io/llhttp_parser.hh"
#include "common/core/logging/log.hh"
#include "common/task/task.pb.h"
#include "common/core/utils/task_id.hh"
#include "common/extensions/scheduler.hh"

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
  task.set_body(std::bit_cast<const char*>(request.body.data()), request.body.size());
  PopulateParametersFromHeaders(task, request.headers);

  const ParamsOnlyRequirementsResolver resolver;
  *task.mutable_requirements() =
      resolver.Resolve(FunctionRef{.type = task.type(), .id = ""}, task.parameters());

  // Fire-and-forget: the scheduler takes ownership of the receiver and MUST
  // eventually resolve it (a result or an error), so the HTTP client never
  // hangs. The scheduler decides node selection, storage registration, and the
  // kTaskSubmission write. Node-selection and frame-writing are deliberately
  // absent here.
  auto receiver = make_receiver_(conn);

  // Clean up the receiver if the HTTP client drops before the task completes.
  // Registered before Schedule so a synchronous error delivery (e.g. no node)
  // cannot race the close callback: both run on the event-loop thread.
  conn.Mailbox()->RegisterOnClose(
      [&storage = storage_, task_id]() -> void { storage.NotifyClientDisconnected(task_id); });

  scheduler_.Schedule(task, std::move(receiver));

  LOG_DEBUG("Submitted task {} (type {}) to scheduler", task_id, task_type.value());
}

} // namespace strij::gateway