#include "core/config/config_loader.hh"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "gateway.pb.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "nodeagent.pb.h"
#include "options.pb.h"
#include "yaml-cpp/yaml.h"

namespace carrot::config {

namespace {

// Forward declarations
void setFieldFromString(google::protobuf::Message* message,
                        const google::protobuf::FieldDescriptor* field, const std::string& value,
                        const google::protobuf::Reflection* reflection, ConfigLoadResult& result);

void mergeYamlIntoProto(const YAML::Node& yaml, google::protobuf::Message* message,
                        const std::string& prefix, ConfigLoadResult& result);

auto fileExists(const std::string& path) -> bool { return std::filesystem::exists(path); }

auto resolveMessageType(const std::string& type_name) -> const google::protobuf::Descriptor* {
  return google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);
}

auto toUpper(std::string_view str) -> std::string {
  std::string result;
  result.reserve(str.size());
  for (char chr : str) {
    result += static_cast<char>(std::toupper(chr));
  }
  return result;
}

auto parseYamlFile(const std::string& path, ConfigLoadResult& result) -> YAML::Node {
  try {
    return YAML::LoadFile(path);
  } catch (const YAML::Exception& e) {
    result.success = false;
    result.error_message = absl::StrCat("YAML parse error: ", e.what());
    result.error_file = path;
    result.error_line = e.mark.line + 1;
    result.error_column = e.mark.column + 1;
    return YAML::Node();
  }
}

void mergeAnyYamlField(const YAML::Node& any_yaml, google::protobuf::Message* message,
                       const google::protobuf::FieldDescriptor* field,
                       const std::string& full_path, ConfigLoadResult& result) {
  const auto* field_msg_type = field->message_type();
  if (field_msg_type == nullptr ||
      field_msg_type->full_name() != "google.protobuf.Any") {
    return;
  }

  if (!any_yaml["@type"]) {
    result.success = false;
    result.error_message =
        absl::StrCat("Field '", full_path, "': google.protobuf.Any requires '@type' key");
    return;
  }

  std::string type_name = any_yaml["@type"].as<std::string>();
  const std::string type_prefix = "type.googleapis.com/";
  if (type_name.starts_with(type_prefix)) {
    type_name = type_name.substr(type_prefix.size());
  }

  const auto* actual_type = resolveMessageType(type_name);
  if (actual_type == nullptr) {
    result.success = false;
    result.error_message =
        absl::StrCat("Field '", full_path, "': unknown type '", type_name, "'");
    return;
  }

  auto* msg_factory = google::protobuf::MessageFactory::generated_factory();
  if (msg_factory == nullptr) {
    result.success = false;
    result.error_message =
        absl::StrCat("Field '", full_path, "': could not get message factory");
    return;
  }

  const auto* prototype = msg_factory->GetPrototype(actual_type);
  if (prototype == nullptr) {
    result.success = false;
    result.error_message =
        absl::StrCat("Field '", full_path, "': could not get prototype for type '",
                     type_name, "'");
    return;
  }

  std::unique_ptr<google::protobuf::Message> owned_msg(prototype->New());
  if (owned_msg == nullptr) {
    result.success = false;
    result.error_message =
        absl::StrCat("Field '", full_path, "': could not create message for type '",
                     type_name, "'");
    return;
  }

  YAML::Node inner_yaml;
  for (const auto& kv_inner : any_yaml) {
    std::string key = kv_inner.first.as<std::string>();
    if (key != "@type") {
      inner_yaml[key] = kv_inner.second;
    }
  }

  mergeYamlIntoProto(inner_yaml, owned_msg.get(), full_path, result);
  if (!result.success) {
    return;
  }

  auto* reflection = message->GetReflection();
  auto* any_field = reflection->MutableMessage(message, field);
  auto* any_msg = dynamic_cast<::google::protobuf::Any*>(any_field);
  if (any_msg != nullptr) {
    any_msg->PackFrom(*owned_msg);
  }
}

void mergeYamlIntoProto(const YAML::Node& yaml, google::protobuf::Message* message,
                        const std::string& prefix, ConfigLoadResult& result) {
  const auto* descriptor = message->GetDescriptor();
  const auto* reflection = message->GetReflection();

  for (const auto& kv : yaml) {
    const auto key = kv.first.as<std::string>();
    const std::string full_path = prefix.empty() ? key : absl::StrCat(prefix, ".", key);

    const auto* field = descriptor->FindFieldByName(key);
    if (field == nullptr) {
      result.warnings.push_back(absl::StrCat("Unknown field '", full_path, "' ignored"));
      continue;
    }

    if (field->is_repeated()) {
      if (!kv.second.IsSequence()) {
        result.success = false;
        result.error_message = absl::StrCat("Field '", full_path, "' expected sequence");
        return;
      }
      for (const auto& item : kv.second) {
        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
          auto* sub_msg = reflection->AddMessage(message, field);
          mergeYamlIntoProto(item, sub_msg, full_path, result);
          if (!result.success) {
            return;
          }
        } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
          reflection->AddString(message, field, item.as<std::string>());
        } else {
          result.success = false;
          result.error_message = absl::StrCat(
              "Repeated field '", full_path, "' only supports message or string type");
          return;
        }
      }
    } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (!kv.second.IsMap()) {
        result.success = false;
        result.error_message = absl::StrCat("Field '", full_path, "' expected map");
        return;
      }

      const auto* field_msg_type = field->message_type();
      if (field_msg_type != nullptr &&
          field_msg_type->full_name() == "google.protobuf.Any") {
        mergeAnyYamlField(kv.second, message, field, full_path, result);
        if (!result.success) {
          return;
        }
      } else {
        auto* sub_msg = reflection->MutableMessage(message, field);
        mergeYamlIntoProto(kv.second, sub_msg, full_path, result);
        if (!result.success) {
          return;
        }
      }
    } else {
      setFieldFromString(message, field, kv.second.as<std::string>(), reflection, result);
      if (!result.success) {
        return;
      }
    }
  }
}

void discoverRepeatedEnvFields(google::protobuf::Message* message,
                               const google::protobuf::FieldDescriptor* field,
                               const std::string& env_prefix, const std::string& path,
                               ConfigLoadResult& result) {
  const auto* sub_descriptor = field->message_type();
  const auto* reflection = message->GetReflection();

  for (int idx = 0;; ++idx) {
    std::string idx_prefix = path + "__" + std::to_string(idx) + "__";
    bool any_found = false;
    for (int j = 0; j < sub_descriptor->field_count(); ++j) {
      const auto* sub_field = sub_descriptor->field(j);
      if (sub_field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        continue;
      }

      std::string env_name = env_prefix + idx_prefix + toUpper(sub_field->name());
      const char* val = std::getenv(env_name.c_str());
      if (val != nullptr) {
        if (!any_found) {
          reflection->AddMessage(message, field);
          any_found = true;
        }
        int size = reflection->FieldSize(*message, field);
        auto* sub_msg = reflection->MutableRepeatedMessage(message, field, size - 1);
        const auto* sub_reflection = sub_msg->GetReflection();
        const auto* actual_field = sub_descriptor->FindFieldByName(sub_field->name());
        if (actual_field != nullptr) {
          setFieldFromString(sub_msg, actual_field, std::string(val), sub_reflection, result);
        }
      }
    }
    if (!any_found) {
      break;
    }
  }
}

void discoverAndApplyEnvVars(google::protobuf::Message* message, const std::string& env_prefix,
                             const std::string& upper_prefix, ConfigLoadResult& result) {
  const auto* descriptor = message->GetDescriptor();
  const auto* reflection = message->GetReflection();

  for (int i = 0; i < descriptor->field_count(); ++i) {
    const auto* field = descriptor->field(i);
    const std::string path = upper_prefix.empty()
                                ? toUpper(field->name())
                                : upper_prefix + "_" + toUpper(field->name());

    if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->is_repeated()) {
        discoverRepeatedEnvFields(message, field, env_prefix, path, result);
      } else {
        discoverAndApplyEnvVars(reflection->MutableMessage(message, field), env_prefix, path,
                                result);
      }
    } else {
      std::string env_name = env_prefix + path;
      const char* val = std::getenv(env_name.c_str());
      if (val != nullptr) {
        setFieldFromString(message, field, std::string(val), reflection, result);
      }
    }
  }
}

void applyEnvOverrides(google::protobuf::Message* message, const std::string& service_prefix,
                       ConfigLoadResult& result) {
  const std::string env_prefix = absl::StrCat("CARROT_", service_prefix, "_");
  discoverAndApplyEnvVars(message, env_prefix, "", result);
}

void applyCliOverridePath(google::protobuf::Message* current,
                          const google::protobuf::Descriptor* descriptor,
                          const google::protobuf::Reflection* reflection,
                          const std::vector<std::string>& parts, size_t depth,
                          const std::string& value, const std::string& path,
                          ConfigLoadResult& result) {
  if (depth >= parts.size()) {
    return;
  }

  const auto* field = descriptor->FindFieldByName(parts[depth]);
  if (field == nullptr) {
    result.warnings.push_back(
        absl::StrCat("CLI override '", path, "': unknown field '", parts[depth], "'"));
    return;
  }

  if (depth + 1 == parts.size()) {
    if (field->is_repeated() &&
        field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      result.warnings.push_back(
          absl::StrCat("CLI override for repeated message not supported: ", path));
    } else {
      setFieldFromString(current, field, value, reflection, result);
    }
    return;
  }

  if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
    result.warnings.push_back(
        absl::StrCat("CLI override '", path, "': expected message field at '", parts[depth], "'"));
    return;
  }

  auto* sub_msg = reflection->MutableMessage(current, field);
  applyCliOverridePath(sub_msg, sub_msg->GetDescriptor(), sub_msg->GetReflection(), parts,
                       depth + 1, value, path, result);
}

void applyCliOverridesInternal(google::protobuf::Message* message,
                               const std::vector<std::string>& overrides,
                               ConfigLoadResult& result) {
  for (const auto& override : overrides) {
    size_t eq_pos = override.find('=');
    if (eq_pos == std::string::npos) {
      continue;
    }

    std::string path = override.substr(0, eq_pos);
    std::string value = override.substr(eq_pos + 1);
    std::vector<std::string> parts = absl::StrSplit(path, ".");

    applyCliOverridePath(message, message->GetDescriptor(), message->GetReflection(), parts, 0,
                         value, path, result);
  }
}

void setFieldFromString(google::protobuf::Message* message,
                        const google::protobuf::FieldDescriptor* field, const std::string& value,
                        const google::protobuf::Reflection* reflection, ConfigLoadResult& result) {
  switch (field->cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
  case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
    reflection->SetInt64(message, field, std::stoll(value));
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
    reflection->SetUInt32(message, field, std::stoul(value));
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
    reflection->SetUInt64(message, field, std::stoull(value));
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    reflection->SetDouble(message, field, std::stod(value));
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    reflection->SetFloat(message, field, std::stof(value));
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
    reflection->SetBool(message, field, value == "true" || value == "1" || value == "yes");
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
  case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
    reflection->SetString(message, field, value);
    break;
  default:
    result.success = false;
    result.error_message =
        absl::StrCat("Unsupported field type for CLI override: ", field->full_name());
    break;
  }
}

struct FieldValueInfo {
  std::string str_val;
  bool is_numeric = false;
  uint64_t num_val = 0;
};

auto extractFieldValue(const google::protobuf::Message& message,
                       const google::protobuf::FieldDescriptor* field,
                       const google::protobuf::Reflection* reflection) -> FieldValueInfo {
  FieldValueInfo info;
  bool repeated = field->is_repeated();

  switch (field->cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
    uint32_t val = repeated ? reflection->GetRepeatedUInt32(message, field, 0)
                            : reflection->GetUInt32(message, field);
    info.str_val = std::to_string(val);
    info.num_val = val;
    info.is_numeric = true;
    break;
  }
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
    uint64_t val = repeated ? reflection->GetRepeatedUInt64(message, field, 0)
                            : reflection->GetUInt64(message, field);
    info.str_val = std::to_string(val);
    info.num_val = val;
    info.is_numeric = true;
    break;
  }
  case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
  case google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
    int64_t val = repeated ? reflection->GetRepeatedInt64(message, field, 0)
                           : reflection->GetInt64(message, field);
    info.str_val = std::to_string(val);
    info.num_val = static_cast<uint64_t>(val);
    info.is_numeric = true;
    break;
  }
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
    double val = repeated ? reflection->GetRepeatedDouble(message, field, 0)
                          : reflection->GetDouble(message, field);
    info.str_val = std::to_string(val);
    break;
  }
  case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
    bool val = repeated ? reflection->GetRepeatedBool(message, field, 0)
                        : reflection->GetBool(message, field);
    info.str_val = val ? "true" : "false";
    break;
  }
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
  case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
    info.str_val = repeated ? reflection->GetRepeatedString(message, field, 0)
                            : reflection->GetString(message, field);
    break;
  }
  default:
    break;
  }

  return info;
}

auto validateFieldValue(const FieldValueInfo& info, const std::string& field_path,
                        const google::protobuf::FieldOptions& ext_opts) -> ConfigLoadResult {
  ConfigLoadResult result;
  result.success = true;

  if (info.is_numeric && ext_opts.HasExtension(carrot::config::range_min)) {
    std::string min_str = ext_opts.GetExtension(carrot::config::range_min);
    uint64_t min_val = std::stoull(min_str);
    if (info.num_val < min_val) {
      result.success = false;
      result.error_message = absl::StrCat("Field '", field_path, "': value ", info.str_val,
                                          " below minimum ", min_str);
      return result;
    }
  }

  if (info.is_numeric && ext_opts.HasExtension(carrot::config::range_max)) {
    std::string max_str = ext_opts.GetExtension(carrot::config::range_max);
    uint64_t max_val = std::stoull(max_str);
    if (info.num_val > max_val) {
      result.success = false;
      result.error_message = absl::StrCat("Field '", field_path, "': value ", info.str_val,
                                          " exceeds maximum ", max_str);
      return result;
    }
  }

  int enum_count = ext_opts.ExtensionSize(carrot::config::enum_values);
  if (enum_count > 0) {
    bool valid = false;
    std::string allowed_str;
    for (int ei = 0; ei < enum_count; ++ei) {
      std::string enum_val = ext_opts.GetExtension(carrot::config::enum_values, ei);
      if (ei > 0) {
        allowed_str += ", ";
      }
      allowed_str += enum_val;
      if (info.str_val == enum_val) {
        valid = true;
      }
    }
    if (!valid) {
      result.success = false;
      result.error_message = absl::StrCat("Field '", field_path, "': value '", info.str_val,
                                          "' not in allowed values: ", allowed_str);
      return result;
    }
  }

  if (ext_opts.HasExtension(carrot::config::pattern)) {
    std::string pattern_str = ext_opts.GetExtension(carrot::config::pattern);
    try {
      std::regex regexp(pattern_str);
      if (!std::regex_match(info.str_val, regexp)) {
        result.success = false;
        result.error_message = absl::StrCat("Field '", field_path, "': value '", info.str_val,
                                            "' does not match pattern '", pattern_str, "'");
        return result;
      }
    } catch (const std::regex_error&) {
      result.warnings.push_back(
          absl::StrCat("Invalid regex pattern for '", field_path, "': ", pattern_str));
    }
  }

  return result;
}

ConfigLoadResult validateMessage(const google::protobuf::Message& message,
                                 const std::string& prefix = {}) {
  ConfigLoadResult result;
  result.success = true;

  const auto* descriptor = message.GetDescriptor();
  const auto* reflection = message.GetReflection();

  for (int i = 0; i < descriptor->field_count(); ++i) {
    const auto* field = descriptor->field(i);

    std::string field_path(prefix);
    if (!prefix.empty()) {
      field_path += ".";
    }
    field_path += field->name();

    if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->is_repeated()) {
        int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          ConfigLoadResult sub_result =
              validateMessage(reflection->GetRepeatedMessage(message, field, j), field_path);
          if (!sub_result.success) {
            return sub_result;
          }
        }
      } else {
        ConfigLoadResult sub_result =
            validateMessage(reflection->GetMessage(message, field), field_path);
        if (!sub_result.success) {
          return sub_result;
        }
      }
      continue;
    }

    bool has_value = reflection->HasField(message, field);
    if (field->is_repeated()) {
      has_value = reflection->FieldSize(message, field) > 0;
    }

    const auto& ext_opts = field->options();

    if (!has_value && ext_opts.HasExtension(carrot::config::required) &&
        ext_opts.GetExtension(carrot::config::required)) {
      result.success = false;
      result.error_message = absl::StrCat("Required field '", field_path, "' is not set");
      return result;
    }

    if (!has_value) {
      continue;
    }

    FieldValueInfo info = extractFieldValue(message, field, reflection);

    ConfigLoadResult field_result = validateFieldValue(info, field_path, ext_opts);
    if (!field_result.success) {
      return field_result;
    }
    result.warnings.insert(result.warnings.end(), field_result.warnings.begin(),
                           field_result.warnings.end());
  }

  return result;
}

} // namespace

template <typename T>
auto LoadConfig(const std::string& config_file_path, const std::vector<std::string>& cli_overrides,
                T* output) -> ConfigLoadResult {
  ConfigLoadResult result;
  result.success = true;

  T config = T::default_instance();

  if (!config_file_path.empty() && fileExists(config_file_path)) {
    YAML::Node yaml = parseYamlFile(config_file_path, result);
    if (!result.success) {
      return result;
    }

    if (!yaml.IsMap()) {
      result.success = false;
      result.error_message = "Config file must be a YAML map";
      result.error_file = config_file_path;
      return result;
    }

    mergeYamlIntoProto(yaml, &config, "", result);
    if (!result.success) {
      return result;
    }
  }

  std::string service_prefix;
  if constexpr (std::is_same_v<T, carrot::config::GatewayConfig>) {
    service_prefix = "GATEWAY";
  } else if constexpr (std::is_same_v<T, carrot::config::NodeAgentConfig>) {
    service_prefix = "NODEAGENT";
  }

  if (!service_prefix.empty()) {
    applyEnvOverrides(&config, service_prefix, result);
  }

  if (!cli_overrides.empty()) {
    applyCliOverridesInternal(&config, cli_overrides, result);
  }

  ConfigLoadResult validation = validateMessage(config);
  if (!validation.success) {
    result.success = false;
    result.error_message = validation.error_message;
    result.error_file = validation.error_file;
    result.error_line = validation.error_line;
    result.error_column = validation.error_column;
    return result;
  }
  result.warnings.insert(result.warnings.end(), validation.warnings.begin(),
                         validation.warnings.end());

  if (output) {
    *output = std::move(config);
  }

  return result;
}

template <typename T> auto ValidateConfig(const T& config) -> ConfigLoadResult {
  return validateMessage(config);
}

template <typename T>
auto ApplyCliOverrides(T& config, const std::vector<std::string>& overrides) -> int {
  ConfigLoadResult result;
  applyCliOverridesInternal(&config, overrides, result);
  return result.success ? 1 : 0;
}

template <typename T> auto GetDefaultConfig() -> T { return T::default_instance(); }

template <typename T> auto ConfigToYaml(const T& config) -> std::string {
  std::string output;
  google::protobuf::TextFormat::PrintToString(config, &output);
  return output;
}

template auto LoadConfig<GatewayConfig>(const std::string&, const std::vector<std::string>&,
                                        GatewayConfig*) -> ConfigLoadResult;

template auto LoadConfig<NodeAgentConfig>(const std::string&, const std::vector<std::string>&,
                                          NodeAgentConfig*) -> ConfigLoadResult;

template auto ValidateConfig<GatewayConfig>(const GatewayConfig&) -> ConfigLoadResult;
template auto ValidateConfig<NodeAgentConfig>(const NodeAgentConfig&) -> ConfigLoadResult;

template auto ApplyCliOverrides<GatewayConfig>(GatewayConfig&, const std::vector<std::string>&)
    -> int;
template auto ApplyCliOverrides<NodeAgentConfig>(NodeAgentConfig&, const std::vector<std::string>&)
    -> int;

template auto GetDefaultConfig<GatewayConfig>() -> GatewayConfig;
template auto GetDefaultConfig<NodeAgentConfig>() -> NodeAgentConfig;

template auto ConfigToYaml<GatewayConfig>(const GatewayConfig&) -> std::string;
template auto ConfigToYaml<NodeAgentConfig>(const NodeAgentConfig&) -> std::string;

} // namespace carrot::config
