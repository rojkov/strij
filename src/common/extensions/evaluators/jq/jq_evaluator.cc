#include "common/extensions/evaluators/jq/jq_evaluator.hh"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "common/extensions/evaluators/jq/jq.pb.h"
#include "jq.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::extensions::evaluators {
namespace {

// Builds the jq_compile_args argument array [{name, value}, ...] from aligned
// names/values; a name without a value binds null. The returned handle is
// consumed by jq_compile_args.
auto buildArgs(std::span<const std::string> names, std::span<const utils::Jv> values) -> jv {
  jv array = jv_array();
  for (size_t i = 0; i < names.size(); ++i) {
    jv entry = jv_object();
    entry = jv_object_set(entry, jv_string("name"), jv_string(names[i].c_str()));
    jv value = i < values.size() ? utils::JvRawCopy(values[i]) : jv_null();
    entry = jv_object_set(entry, jv_string("value"), value);
    array = jv_array_append(array, entry);
  }
  return array;
}

auto compileExpression(jq_state* jqs, std::string_view source, jv args) -> bool {
  // Compile errors (and jq_report_error output) reach the error callback, not
  // jq_get_error_message (that only serves halt/halt_error); the callback is
  // installed by the caller before this runs.
  return jq_compile_args(jqs, std::string(source).c_str(), args) != 0;
}

// Accumulates every jq_report_error message (compile errors, explicit runtime
// error()) so they survive even though jq_get_error_message only covers halt.
struct ErrorCapture {
  std::string text_;
};

void captureError(void* data, jv message) {
  auto* capture = static_cast<ErrorCapture*>(data);
  if (jv_get_kind(message) == JV_KIND_STRING && jv_string_value(message) != nullptr) {
    capture->text_ += jv_string_value(message);
    capture->text_ += "\n";
  }
  jv_free(message);
}

auto stringOf(jv value) -> std::string {
  std::string text;
  if (jv_get_kind(value) == JV_KIND_STRING && jv_string_value(value) != nullptr) {
    text = jv_string_value(value);
  }
  jv_free(value);
  return text;
}

// Layered runtime error extraction: uncaught jq exception (invalid-with-msg
// from jq_next), then halt_error message, then anything err_cb captured.
auto extractRunError(jq_state* jqs, jv output, const ErrorCapture& capture) -> std::string {
  std::string text;
  if (jv_invalid_has_msg(jv_copy(output))) {
    text = stringOf(jv_invalid_get_msg(jv_copy(output)));
  }
  if (text.empty() && jq_halted(jqs)) {
    text = stringOf(jq_get_error_message(jqs));
  }
  if (text.empty()) {
    text = capture.text_;
  }
  return text;
}

} // namespace

JqEvaluator::JqEvaluator(std::string source, std::vector<std::string> variable_names)
    : source_(std::move(source)), variable_names_(std::move(variable_names)) {}

auto JqEvaluator::Run(const utils::Jv& input, std::span<const utils::Jv> args)
    -> absl::StatusOr<std::vector<utils::Jv>> {
  jq_state* jq = jq_init();
  if (jq == nullptr) {
    return absl::InternalError("jq: jq_init failed");
  }
  ErrorCapture capture;
  jq_set_error_cb(jq, captureError, &capture);

  jv arg_array = buildArgs(variable_names_, args);
  if (!compileExpression(jq, source_, arg_array)) {
    std::string message = capture.text_;
    jq_teardown(&jq);
    return absl::InvalidArgumentError(message.empty() ? "jq: failed to compile expression"
                                                      : message);
  }

  jq_start(jq, utils::JvRawCopy(input), 0);
  std::vector<utils::Jv> outputs;
  jv output = jq_next(jq);
  while (jv_is_valid(output)) {
    outputs.push_back(utils::Acquire(output));
    output = jq_next(jq);
  }

  std::string error = extractRunError(jq, output, capture);
  jv_free(output);
  jq_teardown(&jq);

  if (!error.empty()) {
    return absl::InvalidArgumentError(error);
  }
  return outputs;
}

auto JqEvaluatorFactory::Name() const -> std::string { return "jq"; }

auto JqEvaluatorFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<jq::JqEvaluatorConfig>();
}

auto JqEvaluatorFactory::Compile(const ::google::protobuf::Message& /*config*/,
                                 std::string_view source, std::vector<std::string> variable_names,
                                 const EvaluatorDeps& /*deps*/) -> absl::StatusOr<EvaluatorPtr> {
  // Validation compile: bind null placeholders for the declared variables so a
  // source referencing them parses, while a syntax/unbound-variable error
  // surfaces here with jq's message instead of at the first Run.
  jq_state* jq = jq_init();
  if (jq == nullptr) {
    return absl::InternalError("jq: jq_init failed");
  }
  ErrorCapture capture;
  jq_set_error_cb(jq, captureError, &capture);

  jv placeholder_args = buildArgs(variable_names, {});
  if (!compileExpression(jq, source, placeholder_args)) {
    std::string message = capture.text_;
    jq_teardown(&jq);
    return absl::InvalidArgumentError(message.empty() ? "jq: failed to compile expression"
                                                      : message);
  }
  jq_teardown(&jq);

  return std::make_unique<JqEvaluator>(std::string(source), std::move(variable_names));
}

} // namespace strij::extensions::evaluators

REGISTER_FACTORY_FULLY_QUALIFIED(strij::extensions::evaluators::JqEvaluatorFactory,
                                 strij::extensions::evaluators::EvaluatorFactory,
                                 jq_evaluator_registrar)