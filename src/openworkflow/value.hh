#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace strij::openworkflow {

// A yaml-free, JSON-faithful value. Opaque DSL slots (metadata, call arguments,
// set data, event payloads, environment/port/volume mappings, ...) are captured
// as one of these so the model never depends on the YAML representation.
struct Value {
  using Array = std::vector<Value>;
  using Object = std::map<std::string, Value>;
  using Data = std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object>;

  Data data_;

  Value() = default;
  Value(std::monostate null_value) : data_(null_value) {}
  Value(bool boolean_value) : data_(boolean_value) {}
  Value(std::int64_t integer_value) : data_(integer_value) {}
  Value(int integer_value) : data_(static_cast<std::int64_t>(integer_value)) {}
  Value(double double_value) : data_(double_value) {}
  Value(std::string string_value) : data_(std::move(string_value)) {}
  Value(const char* string_value) : data_(std::string(string_value)) {}
  Value(Array array_value) : data_(std::move(array_value)) {}
  Value(Object object_value) : data_(std::move(object_value)) {}

  auto operator==(const Value& other) const -> bool = default;

  [[nodiscard]] auto IsNull() const -> bool {
    return std::holds_alternative<std::monostate>(data_);
  }
  [[nodiscard]] auto IsBool() const -> bool { return std::holds_alternative<bool>(data_); }
  [[nodiscard]] auto IsInt() const -> bool { return std::holds_alternative<std::int64_t>(data_); }
  [[nodiscard]] auto IsDouble() const -> bool { return std::holds_alternative<double>(data_); }
  [[nodiscard]] auto IsString() const -> bool { return std::holds_alternative<std::string>(data_); }
  [[nodiscard]] auto IsArray() const -> bool { return std::holds_alternative<Array>(data_); }
  [[nodiscard]] auto IsObject() const -> bool { return std::holds_alternative<Object>(data_); }

  [[nodiscard]] auto AsBool() const -> bool { return std::get<bool>(data_); }
  [[nodiscard]] auto AsInt() const -> std::int64_t { return std::get<std::int64_t>(data_); }
  [[nodiscard]] auto AsDouble() const -> double { return std::get<double>(data_); }
  [[nodiscard]] auto AsString() const -> const std::string& { return std::get<std::string>(data_); }
  [[nodiscard]] auto AsArray() const -> const Array& { return std::get<Array>(data_); }
  [[nodiscard]] auto AsObject() const -> const Object& { return std::get<Object>(data_); }
};

} // namespace strij::openworkflow
