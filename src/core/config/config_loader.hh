#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace strij::config {

template <typename T>
auto LoadConfig(const std::string& config_file_path,
                const std::vector<std::string>& cli_overrides = {})
    -> absl::StatusOr<T>;

template <typename T> auto ValidateConfig(const T& config) -> absl::Status;

template <typename T>
auto ApplyCliOverrides(T& config, const std::vector<std::string>& overrides) -> absl::Status;

template <typename T> auto GetDefaultConfig() -> T;

template <typename T> auto ConfigToYaml(const T& config) -> std::string;

} // namespace strij::config
