#pragma once

#include <string>
#include <vector>

namespace carrot::config {

struct ConfigLoadResult {
  bool success_ = false;
  std::string error_message_;
  std::string error_file_;
  int error_line_ = 0;
  int error_column_ = 0;
  std::vector<std::string> warnings_;
};

template <typename T>
auto LoadConfig(const std::string& config_file_path,
                const std::vector<std::string>& cli_overrides = {}, T* output = nullptr)
    -> ConfigLoadResult;

template <typename T> auto ValidateConfig(const T& config) -> ConfigLoadResult;

template <typename T>
auto ApplyCliOverrides(T& config, const std::vector<std::string>& overrides) -> int;

template <typename T> auto GetDefaultConfig() -> T;

template <typename T> auto ConfigToYaml(const T& config) -> std::string;

} // namespace carrot::config