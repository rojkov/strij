#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "google/protobuf/message.h"

namespace strij::nodeagent::task_handlers::pyfunction {

// Validated operator configuration. The factory validates through ParseConfig
// (the node-startup path) and the handler only sees this struct.
struct PyFunctionConfig {
  std::uint64_t concurrency_{0};
  std::string python_executable_{"python3"};
  std::string worker_module_;
  std::string worker_executable_;
  std::vector<std::string> python_paths_;
  absl::Duration startup_timeout_{absl::Seconds(30)};
  absl::Duration task_timeout_{absl::ZeroDuration()};
  absl::Duration remote_call_timeout_{absl::Seconds(300)};
  std::size_t max_frame_bytes_{8 * 1024 * 1024};
};

auto ParsePyFunctionConfig(const ::google::protobuf::Message& config)
    -> absl::StatusOr<PyFunctionConfig>;

} // namespace strij::nodeagent::task_handlers::pyfunction
