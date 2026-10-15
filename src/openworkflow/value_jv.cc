#include "openworkflow/value_jv.hh"

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

#include "jv.h"

namespace strij::openworkflow {
namespace {

auto valueToJv(const Value& value) -> jv {
  const Value::Data& data = value.data_;
  if (std::holds_alternative<std::monostate>(data)) {
    return jv_null();
  }
  if (std::holds_alternative<bool>(data)) {
    return std::get<bool>(data) ? jv_true() : jv_false();
  }
  if (std::holds_alternative<std::int64_t>(data)) {
    // A literal keeps the exact decimal text, so the int64 round-trips without
    // passing through double precision.
    const std::string literal = std::to_string(std::get<std::int64_t>(data));
    return jv_number_with_literal(literal.c_str());
  }
  if (std::holds_alternative<double>(data)) {
    return jv_number(std::get<double>(data));
  }
  if (std::holds_alternative<std::string>(data)) {
    const std::string& text = std::get<std::string>(data);
    return jv_string_sized(text.data(), static_cast<int>(text.size()));
  }
  if (std::holds_alternative<Value::Array>(data)) {
    jv array = jv_array();
    for (const Value& element : std::get<Value::Array>(data)) {
      array = jv_array_append(array, valueToJv(element));
    }
    return array;
  }
  jv object = jv_object();
  for (const auto& [key, member] : std::get<Value::Object>(data)) {
    object = jv_object_set(object, jv_string(key.c_str()), valueToJv(member));
  }
  return object;
}

auto isIntegerLiteral(const char* literal) -> std::pair<bool, std::int64_t> {
  const std::string text{literal};
  std::int64_t parsed = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec == std::errc{} && result.ptr == end) {
    return {true, parsed};
  }
  return {false, 0};
}

auto jvToValue(jv value) -> Value {
  switch (jv_get_kind(value)) {
    case JV_KIND_FALSE:
      return Value{false};
    case JV_KIND_TRUE:
      return Value{true};
    case JV_KIND_NUMBER: {
      if (jv_number_has_literal(value) != 0) {
        const auto [is_integer, parsed] = isIntegerLiteral(jv_number_get_literal(value));
        if (is_integer) {
          return Value{parsed};
        }
      }
      return Value{jv_number_value(value)};
    }
    case JV_KIND_STRING: {
      const int length = jv_string_length_bytes(jv_copy(value));
      return Value{std::string(jv_string_value(value), static_cast<std::size_t>(length))};
    }
    case JV_KIND_ARRAY: {
      Value::Array array;
      const int length = jv_array_length(jv_copy(value));
      array.reserve(static_cast<std::size_t>(length));
      for (int index = 0; index < length; ++index) {
        array.push_back(jvToValue(jv_array_get(jv_copy(value), index)));
      }
      return Value{std::move(array)};
    }
    case JV_KIND_OBJECT: {
      Value::Object object;
      for (int iter = jv_object_iter(value); jv_object_iter_valid(value, iter);
           iter = jv_object_iter_next(value, iter)) {
        jv key = jv_object_iter_key(value, iter);
        const int key_length = jv_string_length_bytes(jv_copy(key));
        std::string key_text(jv_string_value(key), static_cast<std::size_t>(key_length));
        jv_free(key);
        object.emplace(std::move(key_text), jvToValue(jv_object_iter_value(value, iter)));
      }
      return Value{std::move(object)};
    }
    case JV_KIND_NULL:
    default:
      return Value{};
  }
}

} // namespace

auto ValueToJv(const Value& value) -> utils::Jv { return utils::Acquire(valueToJv(value)); }

auto JvToValue(const utils::Jv& value) -> Value {
  jv raw = utils::JvRawCopy(value);
  Value result = jvToValue(raw);
  jv_free(raw);
  return result;
}

} // namespace strij::openworkflow
