#include "nodeagent/extensions/task_handlers/pyfunction/config.hh"

#include <cstddef>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "nodeagent/extensions/task_handlers/pyfunction/pyfunction.pb.h"

namespace strij::nodeagent::task_handlers::pyfunction {

using ConfigProto = ::strij::extensions::task_handlers::pyfunction::PyFunctionTaskHandlerConfig;

namespace {

auto validateDuration(const ::google::protobuf::Duration& duration, std::string_view name)
    -> absl::Status {
  if (duration.seconds() < 0 || duration.nanos() < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("pyfunction handler: '", name, "' must not be negative"));
  }
  return absl::OkStatus();
}

auto toDuration(const ::google::protobuf::Duration& duration) -> absl::Duration {
  return absl::Seconds(duration.seconds()) + absl::Nanoseconds(duration.nanos());
}

} // namespace

auto ParsePyFunctionConfig(const ::google::protobuf::Message& config)
    -> absl::StatusOr<PyFunctionConfig> {
  const auto* proto = dynamic_cast<const ConfigProto*>(&config);
  if (proto == nullptr) {
    return absl::InvalidArgumentError("config is not a PyFunctionTaskHandlerConfig");
  }

  PyFunctionConfig out;

  if (proto->capacity().concurrency() == 0) {
    return absl::InvalidArgumentError(
        "pyfunction handler: capacity.concurrency must be at least 1");
  }
  out.concurrency_ = proto->capacity().concurrency();

  if (proto->worker_module().empty() && proto->worker_executable().empty()) {
    return absl::InvalidArgumentError(
        "pyfunction handler: one of worker_module or worker_executable must be set");
  }
  if (!proto->worker_module().empty() && !proto->worker_executable().empty()) {
    return absl::InvalidArgumentError(
        "pyfunction handler: worker_module and worker_executable are mutually exclusive");
  }

  if (!proto->python_executable().empty()) {
    out.python_executable_ = proto->python_executable();
  }
  out.worker_module_ = proto->worker_module();
  out.worker_executable_ = proto->worker_executable();
  out.python_paths_.assign(proto->python_paths().begin(), proto->python_paths().end());

  if (proto->has_startup_timeout()) {
    auto status = validateDuration(proto->startup_timeout(), "startup_timeout");
    if (!status.ok()) {
      return status;
    }
    if (proto->startup_timeout().seconds() != 0 || proto->startup_timeout().nanos() != 0) {
      out.startup_timeout_ = toDuration(proto->startup_timeout());
    }
  }

  if (proto->has_task_timeout()) {
    auto status = validateDuration(proto->task_timeout(), "task_timeout");
    if (!status.ok()) {
      return status;
    }
    out.task_timeout_ = toDuration(proto->task_timeout());
  }

  if (proto->has_remote_call_timeout()) {
    auto status = validateDuration(proto->remote_call_timeout(), "remote_call_timeout");
    if (!status.ok()) {
      return status;
    }
    if (proto->remote_call_timeout().seconds() != 0 || proto->remote_call_timeout().nanos() != 0) {
      out.remote_call_timeout_ = toDuration(proto->remote_call_timeout());
    }
  }

  if (proto->max_frame_bytes() == 0) {
    return absl::InvalidArgumentError(
        "pyfunction handler: 'max_frame_bytes' must be greater than zero");
  }
  out.max_frame_bytes_ = static_cast<std::size_t>(proto->max_frame_bytes());

  return out;
}

} // namespace strij::nodeagent::task_handlers::pyfunction
