#include "openworkflow/parser/decode.hh"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "openworkflow/document.hh"
#include "openworkflow/parser/errors.hh"
#include "openworkflow/parser/one_of.hh"

namespace strij::openworkflow::parser {
namespace {

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const YAML::Mark& mark, const std::string& message) {
  throw ParseError(mark, message);
}

auto getChild(const YAML::Node& node, std::string_view key) -> YAML::Node {
  if (!node.IsMap()) {
    return {};
  }
  return node[std::string(key)];
}

auto has(const YAML::Node& node, std::string_view key) -> bool {
  const YAML::Node child = getChild(node, key);
  return child.IsDefined() && !child.IsNull();
}

void requireMap(const YAML::Node& node, std::string_view what) {
  if (!node.IsMap()) {
    fail(node.Mark(), std::string(what) + " must be a mapping");
  }
}

void requireSequence(const YAML::Node& node, std::string_view what) {
  if (!node.IsSequence()) {
    fail(node.Mark(), std::string(what) + " must be a sequence");
  }
}

void requireKey(const YAML::Node& node, std::string_view key, std::string_view what) {
  if (!has(node, key)) {
    fail(node.Mark(), "missing required key '" + std::string(key) + "' in " + std::string(what));
  }
}

void rejectUnknownKeys(const YAML::Node& node, std::initializer_list<std::string_view> allowed,
                       std::string_view what) {
  for (const auto& entry : node) {
    const std::string key = entry.first.Scalar();
    bool known = false;
    for (const std::string_view candidate : allowed) {
      if (key == candidate) {
        known = true;
        break;
      }
    }
    if (!known) {
      fail(entry.first.Mark(), "unknown key '" + key + "' in " + std::string(what));
    }
  }
}

template <typename T>
auto decodeRequired(const YAML::Node& node, std::string_view key, std::string_view what) -> T {
  const YAML::Node child = getChild(node, key);
  if (!child.IsDefined() || child.IsNull()) {
    fail(node.Mark(), "missing required key '" + std::string(key) + "' in " + std::string(what));
  }
  return child.as<T>();
}

template <typename T>
void decodeOptional(const YAML::Node& node, std::string_view key, std::optional<T>& out) {
  const YAML::Node child = getChild(node, key);
  if (child.IsDefined() && !child.IsNull()) {
    out = child.as<T>();
  }
}

template <typename T> void decodeIfPresent(const YAML::Node& node, std::string_view key, T& out) {
  const YAML::Node child = getChild(node, key);
  if (child.IsDefined() && !child.IsNull()) {
    out = child.as<T>();
  }
}

void decodeStringList(const YAML::Node& node, std::string_view key, std::vector<std::string>& out) {
  const YAML::Node child = getChild(node, key);
  if (child.IsDefined() && !child.IsNull()) {
    requireSequence(child, std::string(key));
    out.clear();
    for (const auto& item : child) {
      out.push_back(item.as<std::string>());
    }
  }
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void decodeValue(const YAML::Node& node, Value& out);
void decodeDurationUnits(const YAML::Node& node, DurationUnits& out);
void decodeDuration(const YAML::Node& node, Duration& out);
void decodeOAuth2Token(const YAML::Node& node, OAuth2Token& out);
void decodeOAuth2Client(const YAML::Node& node, OAuth2AuthenticationProperties::Client& out);
void decodeOAuth2Request(const YAML::Node& node, OAuth2AuthenticationProperties::Request& out);
void decodeOAuth2Endpoints(const YAML::Node& node, OAuth2AuthenticationProperties::Endpoints& out);
void decodeOAuth2Properties(const YAML::Node& node, OAuth2AuthenticationProperties& out);
void decodeBasicAuthentication(const YAML::Node& node, BasicAuthentication& out);
void decodeBearerAuthentication(const YAML::Node& node, BearerAuthentication& out);
void decodeDigestAuthentication(const YAML::Node& node, DigestAuthentication& out);
void decodeOAuth2Authentication(const YAML::Node& node, OAuth2Authentication& out);
void decodeOidcAuthentication(const YAML::Node& node, OidcAuthentication& out);
void decodeAuthentication(const YAML::Node& node, Authentication& out);
void decodeEndpointObject(const YAML::Node& node, EndpointObject& out);
void decodeEndpoint(const YAML::Node& node, Endpoint& out);
void decodeExternalResource(const YAML::Node& node, ExternalResource& out);
void decodeSchema(const YAML::Node& node, Schema& out);
void decodeInput(const YAML::Node& node, Input& out);
void decodeOutput(const YAML::Node& node, Output& out);
void decodeExport(const YAML::Node& node, Export& out);
void decodeError(const YAML::Node& node, Error& out);
void decodeErrorFilter(const YAML::Node& node, ErrorFilter& out);
void decodeBackoff(const YAML::Node& node, Backoff& out);
void decodeJitter(const YAML::Node& node, Jitter& out);
void decodeRetryAttempt(const YAML::Node& node, RetryLimit::Attempt& out);
void decodeRetryLimit(const YAML::Node& node, RetryLimit& out);
void decodeRetryPolicy(const YAML::Node& node, RetryPolicy& out);
void decodeCatalog(const YAML::Node& node, Catalog& out);
void decodeTimeout(const YAML::Node& node, Timeout& out);
void decodeEventProperties(const YAML::Node& node, EventProperties& out);
void decodeCorrelation(const YAML::Node& node, Correlation& out);
void decodeEventFilter(const YAML::Node& node, EventFilter& out);
void decodeEventFilterList(const YAML::Node& node, std::vector<EventFilter>& out);
void decodeEventConsumptionStrategy(const YAML::Node& node, EventConsumptionStrategy& out);
void decodeSubscriptionIterator(const YAML::Node& node, SubscriptionIterator& out);
void decodeContainerLifetime(const YAML::Node& node, ContainerLifetime& out);
void decodeContainerProcess(const YAML::Node& node, ContainerProcess& out);
void decodeScriptProcess(const YAML::Node& node, ScriptProcess& out);
void decodeShellProcess(const YAML::Node& node, ShellProcess& out);
void decodeWorkflowProcess(const YAML::Node& node, WorkflowProcess& out);
void decodeCallBody(const YAML::Node& node, CallBody& out);
void decodeDoBody(const YAML::Node& node, DoBody& out);
void decodeEmitBody(const YAML::Node& node, EmitBody& out);
void decodeEvent(const YAML::Node& node, Event& out);
void decodeForSpec(const YAML::Node& node, ForSpec& out);
void decodeForBody(const YAML::Node& node, ForBody& out);
void decodeForkBody(const YAML::Node& node, ForkBody& out);
void decodeListenBody(const YAML::Node& node, ListenBody& out);
void decodeRaiseBody(const YAML::Node& node, RaiseBody& out);
void decodeRunBody(const YAML::Node& node, RunBody& out);
void decodeSetBody(const YAML::Node& node, SetBody& out);
void decodeSwitchBody(const YAML::Node& node, SwitchBody& out);
void decodeCatch(const YAML::Node& node, Catch& out);
void decodeTryBody(const YAML::Node& node, TryBody& out);
void decodeWaitBody(const YAML::Node& node, WaitBody& out);
void decodeTask(const YAML::Node& node, Task& out);
void decodeTasks(const YAML::Node& node, Tasks& out);
void decodeExtension(const YAML::Node& node, Extension& out);
void decodeUse(const YAML::Node& node, Use& out);
void decodeDocumentInfo(const YAML::Node& node, DocumentInfo& out);
void decodeSchedule(const YAML::Node& node, Schedule& out);
void decodeEvaluate(const YAML::Node& node, Evaluate& out);

// ---------------------------------------------------------------------------
// Leaf and shared shapes
// ---------------------------------------------------------------------------

void decodeValue(const YAML::Node& node, Value& out) {
  if (!node.IsDefined() || node.IsNull()) {
    out = Value();
    return;
  }
  if (node.IsSequence()) {
    Value::Array array;
    array.reserve(node.size());
    for (const auto& item : node) {
      Value element;
      decodeValue(item, element);
      array.push_back(std::move(element));
    }
    out = std::move(array);
    return;
  }
  if (node.IsMap()) {
    Value::Object object;
    for (const auto& entry : node) {
      Value element;
      decodeValue(entry.second, element);
      object.emplace(entry.first.Scalar(), std::move(element));
    }
    out = std::move(object);
    return;
  }

  bool boolean_value = false;
  if (YAML::convert<bool>::decode(node, boolean_value)) {
    out = boolean_value;
    return;
  }
  std::int64_t integer_value = 0;
  if (YAML::convert<std::int64_t>::decode(node, integer_value)) {
    out = integer_value;
    return;
  }
  double double_value = 0.0;
  if (YAML::convert<double>::decode(node, double_value)) {
    out = double_value;
    return;
  }
  out = node.Scalar();
}

void decodeDurationUnits(const YAML::Node& node, DurationUnits& out) {
  requireMap(node, "duration");
  rejectUnknownKeys(node, {"days", "hours", "minutes", "seconds", "milliseconds"}, "duration");
  decodeOptional(node, "days", out.days_);
  decodeOptional(node, "hours", out.hours_);
  decodeOptional(node, "minutes", out.minutes_);
  decodeOptional(node, "seconds", out.seconds_);
  decodeOptional(node, "milliseconds", out.milliseconds_);
}

void decodeDuration(const YAML::Node& node, Duration& out) {
  if (node.IsScalar()) {
    out = node.as<std::string>();
    return;
  }
  if (node.IsMap()) {
    DurationUnits units;
    decodeDurationUnits(node, units);
    out = units;
    return;
  }
  fail(node.Mark(), "duration must be a string or a mapping");
}

void decodeOAuth2Token(const YAML::Node& node, OAuth2Token& out) {
  requireMap(node, "oauth2 token");
  rejectUnknownKeys(node, {"token", "type"}, "oauth2 token");
  out.token_ = decodeRequired<std::string>(node, "token", "oauth2 token");
  out.type_ = decodeRequired<std::string>(node, "type", "oauth2 token");
}

void decodeOAuth2Client(const YAML::Node& node, OAuth2AuthenticationProperties::Client& out) {
  requireMap(node, "oauth2 client");
  rejectUnknownKeys(node, {"id", "secret", "assertion", "authentication"}, "oauth2 client");
  decodeOptional(node, "id", out.id_);
  decodeOptional(node, "secret", out.secret_);
  decodeOptional(node, "assertion", out.assertion_);
  decodeOptional(node, "authentication", out.authentication_);
}

void decodeOAuth2Request(const YAML::Node& node, OAuth2AuthenticationProperties::Request& out) {
  requireMap(node, "oauth2 request");
  rejectUnknownKeys(node, {"encoding"}, "oauth2 request");
  decodeOptional(node, "encoding", out.encoding_);
}

void decodeOAuth2Endpoints(const YAML::Node& node, OAuth2AuthenticationProperties::Endpoints& out) {
  requireMap(node, "oauth2 endpoints");
  rejectUnknownKeys(node, {"token", "introspection"}, "oauth2 endpoints");
  decodeOptional(node, "token", out.token_);
  decodeOptional(node, "introspection", out.introspection_);
}

void decodeOAuth2Properties(const YAML::Node& node, OAuth2AuthenticationProperties& out) {
  requireMap(node, "oauth2 authentication");
  rejectUnknownKeys(node,
                    {"authority", "grant", "client", "request", "endpoints", "issuers", "scopes",
                     "audiences", "username", "password", "subject", "actor"},
                    "oauth2 authentication");
  out.authority_ = decodeRequired<std::string>(node, "authority", "oauth2 authentication");
  out.grant_ = decodeRequired<std::string>(node, "grant", "oauth2 authentication");
  if (has(node, "client")) {
    out.client_.emplace();
    decodeOAuth2Client(getChild(node, "client"), *out.client_);
  }
  if (has(node, "request")) {
    out.request_.emplace();
    decodeOAuth2Request(getChild(node, "request"), *out.request_);
  }
  if (has(node, "endpoints")) {
    out.endpoints_.emplace();
    decodeOAuth2Endpoints(getChild(node, "endpoints"), *out.endpoints_);
  }
  decodeStringList(node, "issuers", out.issuers_);
  decodeStringList(node, "scopes", out.scopes_);
  decodeStringList(node, "audiences", out.audiences_);
  decodeOptional(node, "username", out.username_);
  decodeOptional(node, "password", out.password_);
  if (has(node, "subject")) {
    out.subject_.emplace();
    decodeOAuth2Token(getChild(node, "subject"), *out.subject_);
  }
  if (has(node, "actor")) {
    out.actor_.emplace();
    decodeOAuth2Token(getChild(node, "actor"), *out.actor_);
  }
}

void decodeBasicAuthentication(const YAML::Node& node, BasicAuthentication& out) {
  requireMap(node, "basic authentication");
  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "basic authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "basic authentication reference");
    return;
  }
  rejectUnknownKeys(node, {"username", "password"}, "basic authentication");
  out.username_ = decodeRequired<std::string>(node, "username", "basic authentication");
  out.password_ = decodeRequired<std::string>(node, "password", "basic authentication");
}

void decodeBearerAuthentication(const YAML::Node& node, BearerAuthentication& out) {
  requireMap(node, "bearer authentication");
  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "bearer authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "bearer authentication reference");
    return;
  }
  rejectUnknownKeys(node, {"token"}, "bearer authentication");
  out.token_ = decodeRequired<std::string>(node, "token", "bearer authentication");
}

void decodeDigestAuthentication(const YAML::Node& node, DigestAuthentication& out) {
  requireMap(node, "digest authentication");
  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "digest authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "digest authentication reference");
    return;
  }
  rejectUnknownKeys(node, {"username", "password"}, "digest authentication");
  out.username_ = decodeRequired<std::string>(node, "username", "digest authentication");
  out.password_ = decodeRequired<std::string>(node, "password", "digest authentication");
}

void decodeOAuth2Authentication(const YAML::Node& node, OAuth2Authentication& out) {
  requireMap(node, "oauth2 authentication");
  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "oauth2 authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "oauth2 authentication reference");
    return;
  }
  out.properties_.emplace();
  decodeOAuth2Properties(node, *out.properties_);
}

void decodeOidcAuthentication(const YAML::Node& node, OidcAuthentication& out) {
  requireMap(node, "oidc authentication");
  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "oidc authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "oidc authentication reference");
    return;
  }
  out.properties_.emplace();
  decodeOAuth2Properties(node, *out.properties_);
}

void decodeAuthentication(const YAML::Node& node, Authentication& out) {
  requireMap(node, "authentication");
  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "authentication");
    out.scheme_ =
        AuthenticationReference{decodeRequired<std::string>(node, "use", "authentication")};
    return;
  }
  const std::string_view scheme =
      SelectOne(node, {"basic", "bearer", "digest", "oauth2", "oidc"}, "authentication scheme");
  rejectUnknownKeys(node, {"basic", "bearer", "digest", "oauth2", "oidc"}, "authentication");
  const YAML::Node scheme_node = getChild(node, scheme);
  if (scheme == "basic") {
    BasicAuthentication value;
    decodeBasicAuthentication(scheme_node, value);
    out.scheme_ = std::move(value);
  } else if (scheme == "bearer") {
    BearerAuthentication value;
    decodeBearerAuthentication(scheme_node, value);
    out.scheme_ = std::move(value);
  } else if (scheme == "digest") {
    DigestAuthentication value;
    decodeDigestAuthentication(scheme_node, value);
    out.scheme_ = std::move(value);
  } else if (scheme == "oauth2") {
    OAuth2Authentication value;
    decodeOAuth2Authentication(scheme_node, value);
    out.scheme_ = std::move(value);
  } else {
    OidcAuthentication value;
    decodeOidcAuthentication(scheme_node, value);
    out.scheme_ = std::move(value);
  }
}

void decodeEndpointObject(const YAML::Node& node, EndpointObject& out) {
  requireMap(node, "endpoint");
  rejectUnknownKeys(node, {"uri", "authentication"}, "endpoint");
  out.uri_ = decodeRequired<std::string>(node, "uri", "endpoint");
  if (has(node, "authentication")) {
    out.authentication_.emplace();
    decodeAuthentication(getChild(node, "authentication"), *out.authentication_);
  }
}

void decodeEndpoint(const YAML::Node& node, Endpoint& out) {
  if (node.IsScalar()) {
    out.value_ = node.as<std::string>();
    return;
  }
  if (node.IsMap()) {
    EndpointObject value;
    decodeEndpointObject(node, value);
    out.value_ = std::move(value);
    return;
  }
  fail(node.Mark(), "endpoint must be a string or a mapping");
}

void decodeExternalResource(const YAML::Node& node, ExternalResource& out) {
  requireMap(node, "external resource");
  rejectUnknownKeys(node, {"name", "endpoint"}, "external resource");
  decodeOptional(node, "name", out.name_);
  requireKey(node, "endpoint", "external resource");
  decodeEndpoint(getChild(node, "endpoint"), out.endpoint_);
}

void decodeSchema(const YAML::Node& node, Schema& out) {
  requireMap(node, "schema");
  rejectUnknownKeys(node, {"format", "document", "resource"}, "schema");
  decodeIfPresent(node, "format", out.format_);
  const std::string_view source = SelectOne(node, {"document", "resource"}, "schema source");
  const YAML::Node source_node = getChild(node, source);
  if (source == "document") {
    Value value;
    decodeValue(source_node, value);
    out.source_ = std::move(value);
  } else {
    ExternalResource value;
    decodeExternalResource(source_node, value);
    out.source_ = std::move(value);
  }
}

void decodeInput(const YAML::Node& node, Input& out) {
  requireMap(node, "input");
  rejectUnknownKeys(node, {"schema", "from"}, "input");
  if (has(node, "schema")) {
    out.schema_.emplace();
    decodeSchema(getChild(node, "schema"), *out.schema_);
  }
  if (has(node, "from")) {
    const YAML::Node child = getChild(node, "from");
    if (child.IsScalar()) {
      out.from_ = child.as<std::string>();
    } else {
      Value value;
      decodeValue(child, value);
      out.from_ = std::move(value);
    }
  }
}

void decodeOutput(const YAML::Node& node, Output& out) {
  requireMap(node, "output");
  rejectUnknownKeys(node, {"schema", "as"}, "output");
  if (has(node, "schema")) {
    out.schema_.emplace();
    decodeSchema(getChild(node, "schema"), *out.schema_);
  }
  if (has(node, "as")) {
    const YAML::Node child = getChild(node, "as");
    if (child.IsScalar()) {
      out.as_ = child.as<std::string>();
    } else {
      Value value;
      decodeValue(child, value);
      out.as_ = std::move(value);
    }
  }
}

void decodeExport(const YAML::Node& node, Export& out) {
  requireMap(node, "export");
  rejectUnknownKeys(node, {"schema", "as"}, "export");
  if (has(node, "schema")) {
    out.schema_.emplace();
    decodeSchema(getChild(node, "schema"), *out.schema_);
  }
  if (has(node, "as")) {
    const YAML::Node child = getChild(node, "as");
    if (child.IsScalar()) {
      out.as_ = child.as<std::string>();
    } else {
      Value value;
      decodeValue(child, value);
      out.as_ = std::move(value);
    }
  }
}

void decodeError(const YAML::Node& node, Error& out) {
  requireMap(node, "error");
  rejectUnknownKeys(node, {"type", "status", "instance", "title", "detail"}, "error");
  out.type_ = decodeRequired<std::string>(node, "type", "error");
  out.status_ = decodeRequired<std::int64_t>(node, "status", "error");
  decodeOptional(node, "instance", out.instance_);
  decodeOptional(node, "title", out.title_);
  decodeOptional(node, "detail", out.detail_);
}

void decodeErrorFilter(const YAML::Node& node, ErrorFilter& out) {
  requireMap(node, "error filter");
  rejectUnknownKeys(node, {"type", "status", "instance", "title", "detail"}, "error filter");
  decodeOptional(node, "type", out.type_);
  decodeOptional(node, "status", out.status_);
  decodeOptional(node, "instance", out.instance_);
  decodeOptional(node, "title", out.title_);
  decodeOptional(node, "detail", out.detail_);
}

void decodeBackoff(const YAML::Node& node, Backoff& out) {
  requireMap(node, "retry backoff");
  const std::string_view kind =
      SelectOne(node, {"constant", "exponential", "linear"}, "retry backoff branch");
  rejectUnknownKeys(node, {"constant", "exponential", "linear"}, "retry backoff");
  out.kind_ = std::string(kind);
  decodeValue(getChild(node, kind), out.parameters_);
}

void decodeJitter(const YAML::Node& node, Jitter& out) {
  requireMap(node, "retry jitter");
  rejectUnknownKeys(node, {"from", "to"}, "retry jitter");
  requireKey(node, "from", "retry jitter");
  requireKey(node, "to", "retry jitter");
  decodeDuration(getChild(node, "from"), out.from_);
  decodeDuration(getChild(node, "to"), out.to_);
}

void decodeRetryAttempt(const YAML::Node& node, RetryLimit::Attempt& out) {
  requireMap(node, "retry attempt");
  rejectUnknownKeys(node, {"count", "duration"}, "retry attempt");
  decodeOptional(node, "count", out.count_);
  if (has(node, "duration")) {
    decodeDuration(getChild(node, "duration"), out.duration_.emplace());
  }
}

void decodeRetryLimit(const YAML::Node& node, RetryLimit& out) {
  requireMap(node, "retry limit");
  rejectUnknownKeys(node, {"attempt", "duration"}, "retry limit");
  if (has(node, "attempt")) {
    out.attempt_.emplace();
    decodeRetryAttempt(getChild(node, "attempt"), *out.attempt_);
  }
  if (has(node, "duration")) {
    decodeDuration(getChild(node, "duration"), out.duration_.emplace());
  }
}

void decodeRetryPolicy(const YAML::Node& node, RetryPolicy& out) {
  requireMap(node, "retry policy");
  rejectUnknownKeys(node, {"when", "exceptWhen", "delay", "backoff", "limit", "jitter"},
                    "retry policy");
  decodeOptional(node, "when", out.when_);
  decodeOptional(node, "exceptWhen", out.exceptWhen_);
  if (has(node, "delay")) {
    decodeDuration(getChild(node, "delay"), out.delay_.emplace());
  }
  if (has(node, "backoff")) {
    out.backoff_.emplace();
    decodeBackoff(getChild(node, "backoff"), *out.backoff_);
  }
  if (has(node, "limit")) {
    out.limit_.emplace();
    decodeRetryLimit(getChild(node, "limit"), *out.limit_);
  }
  if (has(node, "jitter")) {
    out.jitter_.emplace();
    decodeJitter(getChild(node, "jitter"), *out.jitter_);
  }
}

void decodeCatalog(const YAML::Node& node, Catalog& out) {
  requireMap(node, "catalog");
  rejectUnknownKeys(node, {"endpoint"}, "catalog");
  requireKey(node, "endpoint", "catalog");
  decodeEndpoint(getChild(node, "endpoint"), out.endpoint_);
}

void decodeTimeout(const YAML::Node& node, Timeout& out) {
  requireMap(node, "timeout");
  rejectUnknownKeys(node, {"after"}, "timeout");
  requireKey(node, "after", "timeout");
  decodeDuration(getChild(node, "after"), out.after_);
}

// ---------------------------------------------------------------------------
// Event machinery
// ---------------------------------------------------------------------------

void decodeEventProperties(const YAML::Node& node, EventProperties& out) {
  requireMap(node, "event properties");
  rejectUnknownKeys(
      node, {"id", "source", "type", "time", "subject", "datacontenttype", "dataschema", "data"},
      "event properties");
  decodeOptional(node, "id", out.id_);
  decodeOptional(node, "source", out.source_);
  decodeOptional(node, "type", out.type_);
  decodeOptional(node, "time", out.time_);
  decodeOptional(node, "subject", out.subject_);
  decodeOptional(node, "datacontenttype", out.datacontenttype_);
  decodeOptional(node, "dataschema", out.dataschema_);
  if (has(node, "data")) {
    out.data_.emplace();
    decodeValue(getChild(node, "data"), *out.data_);
  }
}

void decodeCorrelation(const YAML::Node& node, Correlation& out) {
  requireMap(node, "correlation");
  rejectUnknownKeys(node, {"from", "expect"}, "correlation");
  out.from_ = decodeRequired<std::string>(node, "from", "correlation");
  decodeOptional(node, "expect", out.expect_);
}

void decodeEventFilter(const YAML::Node& node, EventFilter& out) {
  requireMap(node, "event filter");
  rejectUnknownKeys(node, {"with", "correlate"}, "event filter");
  requireKey(node, "with", "event filter");
  decodeEventProperties(getChild(node, "with"), out.with_);
  if (has(node, "correlate")) {
    const YAML::Node correlate = getChild(node, "correlate");
    requireMap(correlate, "event correlation");
    for (const auto& entry : correlate) {
      Correlation value;
      decodeCorrelation(entry.second, value);
      out.correlate_.emplace(entry.first.Scalar(), std::move(value));
    }
  }
}

void decodeEventFilterList(const YAML::Node& node, std::vector<EventFilter>& out) {
  requireSequence(node, "event list");
  out.clear();
  for (const auto& item : node) {
    EventFilter value;
    decodeEventFilter(item, value);
    out.push_back(std::move(value));
  }
}

void decodeEventConsumptionStrategy(const YAML::Node& node, EventConsumptionStrategy& out) {
  requireMap(node, "event consumption strategy");
  const std::string_view mode =
      SelectOne(node, {"all", "any", "one"}, "event consumption strategy");
  rejectUnknownKeys(node, {"all", "any", "one", "until"}, "event consumption strategy");
  if (mode == "all") {
    out.mode_ = EventConsumptionStrategy::Mode::kAll;
    decodeEventFilterList(getChild(node, "all"), out.filters_);
  } else if (mode == "any") {
    out.mode_ = EventConsumptionStrategy::Mode::kAny;
    decodeEventFilterList(getChild(node, "any"), out.filters_);
    if (has(node, "until")) {
      out.until_.emplace();
      decodeValue(getChild(node, "until"), *out.until_);
    }
  } else {
    out.mode_ = EventConsumptionStrategy::Mode::kOne;
    EventFilter value;
    decodeEventFilter(getChild(node, "one"), value);
    out.filters_.push_back(std::move(value));
  }
}

void decodeSubscriptionIterator(const YAML::Node& node, SubscriptionIterator& out) {
  requireMap(node, "subscription iterator");
  rejectUnknownKeys(node, {"item", "at", "do", "output", "export"}, "subscription iterator");
  decodeOptional(node, "item", out.item_);
  decodeOptional(node, "at", out.at_);
  if (has(node, "do")) {
    decodeTasks(getChild(node, "do"), out.do_);
  }
  if (has(node, "output")) {
    out.output_.emplace();
    decodeOutput(getChild(node, "output"), *out.output_);
  }
  if (has(node, "export")) {
    out.export_.emplace();
    decodeExport(getChild(node, "export"), *out.export_);
  }
}

// ---------------------------------------------------------------------------
// Task bodies
// ---------------------------------------------------------------------------

void decodeContainerLifetime(const YAML::Node& node, ContainerLifetime& out) {
  requireMap(node, "container lifetime");
  rejectUnknownKeys(node, {"cleanup", "after"}, "container lifetime");
  out.cleanup_ = decodeRequired<std::string>(node, "cleanup", "container lifetime");
  if (has(node, "after")) {
    decodeDuration(getChild(node, "after"), out.after_.emplace());
  }
}

void decodeContainerProcess(const YAML::Node& node, ContainerProcess& out) {
  requireMap(node, "container process");
  rejectUnknownKeys(node,
                    {"image", "name", "command", "ports", "volumes", "environment", "stdin",
                     "arguments", "lifetime", "pullPolicy"},
                    "container process");
  out.image_ = decodeRequired<std::string>(node, "image", "container process");
  decodeOptional(node, "name", out.name_);
  decodeOptional(node, "command", out.command_);
  if (has(node, "ports")) {
    out.ports_.emplace();
    decodeValue(getChild(node, "ports"), *out.ports_);
  }
  if (has(node, "volumes")) {
    out.volumes_.emplace();
    decodeValue(getChild(node, "volumes"), *out.volumes_);
  }
  if (has(node, "environment")) {
    out.environment_.emplace();
    decodeValue(getChild(node, "environment"), *out.environment_);
  }
  decodeOptional(node, "stdin", out.stdin_);
  decodeStringList(node, "arguments", out.arguments_);
  if (has(node, "lifetime")) {
    out.lifetime_.emplace();
    decodeContainerLifetime(getChild(node, "lifetime"), *out.lifetime_);
  }
  decodeOptional(node, "pullPolicy", out.pull_policy_);
}

void decodeScriptProcess(const YAML::Node& node, ScriptProcess& out) {
  requireMap(node, "script process");
  const std::string_view source = SelectOne(node, {"code", "resource"}, "script process source");
  rejectUnknownKeys(node, {"language", "stdin", "arguments", "environment", "code", "resource"},
                    "script process");
  out.language_ = decodeRequired<std::string>(node, "language", "script process");
  decodeOptional(node, "stdin", out.stdin_);
  decodeStringList(node, "arguments", out.arguments_);
  if (has(node, "environment")) {
    out.environment_.emplace();
    decodeValue(getChild(node, "environment"), *out.environment_);
  }
  if (source == "code") {
    out.source_ = ScriptSourceCode{decodeRequired<std::string>(node, "code", "script process")};
  } else {
    ScriptSourceResource resource;
    decodeExternalResource(getChild(node, "resource"), resource.source_);
    out.source_ = std::move(resource);
  }
}

void decodeShellProcess(const YAML::Node& node, ShellProcess& out) {
  requireMap(node, "shell process");
  rejectUnknownKeys(node, {"command", "stdin", "arguments", "environment"}, "shell process");
  out.command_ = decodeRequired<std::string>(node, "command", "shell process");
  decodeOptional(node, "stdin", out.stdin_);
  decodeStringList(node, "arguments", out.arguments_);
  if (has(node, "environment")) {
    out.environment_.emplace();
    decodeValue(getChild(node, "environment"), *out.environment_);
  }
}

void decodeWorkflowProcess(const YAML::Node& node, WorkflowProcess& out) {
  requireMap(node, "workflow process");
  rejectUnknownKeys(node, {"namespace", "name", "version", "input"}, "workflow process");
  out.namespace_ = decodeRequired<std::string>(node, "namespace", "workflow process");
  out.name_ = decodeRequired<std::string>(node, "name", "workflow process");
  out.version_ = decodeRequired<std::string>(node, "version", "workflow process");
  if (has(node, "input")) {
    out.input_.emplace();
    decodeValue(getChild(node, "input"), *out.input_);
  }
}

void decodeCallBody(const YAML::Node& node, CallBody& out) {
  requireKey(node, "call", "call task");
  out.call_ = decodeRequired<std::string>(node, "call", "call task");
  if (has(node, "with")) {
    decodeValue(getChild(node, "with"), out.with_);
  }
}

void decodeDoBody(const YAML::Node& node, DoBody& out) {
  requireKey(node, "do", "do task");
  decodeTasks(getChild(node, "do"), out.do_);
}

void decodeEvent(const YAML::Node& node, Event& out) {
  requireMap(node, "event");
  rejectUnknownKeys(node, {"with"}, "event");
  if (has(node, "with")) {
    decodeEventProperties(getChild(node, "with"), out.with_);
  }
}

void decodeEmitBody(const YAML::Node& node, EmitBody& out) {
  requireKey(node, "emit", "emit task");
  const YAML::Node emit = getChild(node, "emit");
  requireMap(emit, "emit task");
  rejectUnknownKeys(emit, {"event"}, "emit task");
  requireKey(emit, "event", "emit task");
  decodeEvent(getChild(emit, "event"), out.event_);
}

void decodeForSpec(const YAML::Node& node, ForSpec& out) {
  requireMap(node, "for");
  rejectUnknownKeys(node, {"each", "at", "in"}, "for");
  decodeOptional(node, "each", out.each_);
  decodeOptional(node, "at", out.at_);
  requireKey(node, "in", "for");
  const YAML::Node in_key = getChild(node, "in");
  if (in_key.IsScalar()) {
    out.in_ = in_key.as<std::string>();
  } else {
    Value value;
    decodeValue(in_key, value);
    out.in_ = std::move(value);
  }
}

void decodeForBody(const YAML::Node& node, ForBody& out) {
  requireKey(node, "for", "for task");
  decodeForSpec(getChild(node, "for"), out.for_);
  decodeOptional(node, "while", out.while_);
  requireKey(node, "do", "for task");
  decodeTasks(getChild(node, "do"), out.do_);
}

void decodeForkBody(const YAML::Node& node, ForkBody& out) {
  requireKey(node, "fork", "fork task");
  const YAML::Node fork = getChild(node, "fork");
  requireMap(fork, "fork task");
  rejectUnknownKeys(fork, {"branches", "compete"}, "fork task");
  requireKey(fork, "branches", "fork task");
  decodeTasks(getChild(fork, "branches"), out.branches_);
  decodeIfPresent(fork, "compete", out.compete_);
}

void decodeListenBody(const YAML::Node& node, ListenBody& out) {
  requireKey(node, "listen", "listen task");
  const YAML::Node listen = getChild(node, "listen");
  requireMap(listen, "listen task");
  rejectUnknownKeys(listen, {"to", "read"}, "listen task");
  requireKey(listen, "to", "listen task");
  decodeEventConsumptionStrategy(getChild(listen, "to"), out.to_);
  decodeOptional(listen, "read", out.read_);
  if (has(node, "foreach")) {
    out.foreach_.emplace();
    decodeSubscriptionIterator(getChild(node, "foreach"), *out.foreach_);
  }
}

void decodeRaiseBody(const YAML::Node& node, RaiseBody& out) {
  requireKey(node, "raise", "raise task");
  const YAML::Node raise = getChild(node, "raise");
  requireMap(raise, "raise task");
  rejectUnknownKeys(raise, {"error"}, "raise task");
  requireKey(raise, "error", "raise task");
  const YAML::Node error = getChild(raise, "error");
  if (error.IsScalar()) {
    out.error_ = error.as<std::string>();
  } else {
    Error value;
    decodeError(error, value);
    out.error_ = std::move(value);
  }
}

void decodeRunBody(const YAML::Node& node, RunBody& out) {
  requireKey(node, "run", "run task");
  const YAML::Node run = getChild(node, "run");
  requireMap(run, "run task");
  const std::string_view kind =
      SelectOne(run, {"container", "shell", "script", "workflow"}, "run process kind");
  rejectUnknownKeys(run, {"container", "shell", "script", "workflow", "await", "return"},
                    "run task");
  const YAML::Node process = getChild(run, kind);
  if (kind == "container") {
    ContainerProcess value;
    decodeContainerProcess(process, value);
    out.process_ = std::move(value);
  } else if (kind == "shell") {
    ShellProcess value;
    decodeShellProcess(process, value);
    out.process_ = std::move(value);
  } else if (kind == "script") {
    ScriptProcess value;
    decodeScriptProcess(process, value);
    out.process_ = std::move(value);
  } else {
    WorkflowProcess value;
    decodeWorkflowProcess(process, value);
    out.process_ = std::move(value);
  }
  decodeOptional(run, "await", out.await_);
  decodeOptional(run, "return", out.return_);
}

void decodeSetBody(const YAML::Node& node, SetBody& out) {
  requireKey(node, "set", "set task");
  const YAML::Node set = getChild(node, "set");
  if (set.IsScalar()) {
    out.set_ = set.as<std::string>();
  } else {
    Value value;
    decodeValue(set, value);
    out.set_ = std::move(value);
  }
}

void decodeSwitchBody(const YAML::Node& node, SwitchBody& out) {
  requireKey(node, "switch", "switch task");
  const YAML::Node node_switch = getChild(node, "switch");
  requireSequence(node_switch, "switch task");
  out.cases_.clear();
  for (const auto& item : node_switch) {
    requireMap(item, "switch case");
    if (item.size() != 1) {
      fail(item.Mark(), "each switch case must have exactly one name");
    }
    const auto entry = *item.begin();
    NamedCase named_case;
    named_case.name_ = entry.first.Scalar();
    const YAML::Node body = entry.second;
    requireMap(body, "switch case");
    rejectUnknownKeys(body, {"when", "then"}, "switch case");
    decodeOptional(body, "when", named_case.when_);
    decodeOptional(body, "then", named_case.then_);
    out.cases_.push_back(std::move(named_case));
  }
}

void decodeCatch(const YAML::Node& node, Catch& out) {
  requireMap(node, "catch");
  rejectUnknownKeys(node, {"errors", "as", "when", "exceptWhen", "retry", "do", "then"}, "catch");
  if (has(node, "errors")) {
    const YAML::Node errors = getChild(node, "errors");
    requireMap(errors, "catch errors");
    rejectUnknownKeys(errors, {"with"}, "catch errors");
    if (has(errors, "with")) {
      out.errors_.emplace();
      decodeErrorFilter(getChild(errors, "with"), *out.errors_);
    }
  }
  decodeOptional(node, "as", out.as_);
  decodeOptional(node, "when", out.when_);
  decodeOptional(node, "exceptWhen", out.exceptWhen_);
  if (has(node, "retry")) {
    const YAML::Node retry = getChild(node, "retry");
    if (retry.IsScalar()) {
      out.retry_ = retry.as<std::string>();
    } else {
      RetryPolicy value;
      decodeRetryPolicy(retry, value);
      out.retry_ = std::move(value);
    }
  }
  if (has(node, "do")) {
    decodeTasks(getChild(node, "do"), out.do_);
  }
  decodeOptional(node, "then", out.then_);
}

void decodeTryBody(const YAML::Node& node, TryBody& out) {
  requireKey(node, "try", "try task");
  decodeTasks(getChild(node, "try"), out.try_);
  if (has(node, "catch")) {
    out.catch_.emplace();
    decodeCatch(getChild(node, "catch"), *out.catch_);
  }
}

void decodeWaitBody(const YAML::Node& node, WaitBody& out) {
  requireKey(node, "wait", "wait task");
  decodeDuration(getChild(node, "wait"), out.wait_);
}

// ---------------------------------------------------------------------------
// Task union and collections
// ---------------------------------------------------------------------------

void decodeTask(const YAML::Node& node, Task& out) {
  requireMap(node, "task");
  rejectUnknownKeys(node,
                    {"if",     "input", "output", "export", "timeout", "then",  "metadata", "call",
                     "do",     "emit",  "for",    "fork",   "listen",  "raise", "run",      "set",
                     "switch", "try",   "wait",   "with",   "foreach", "while", "catch"},
                    "task");
  decodeOptional(node, "if", out.if_);
  if (has(node, "input")) {
    out.input_.emplace();
    decodeInput(getChild(node, "input"), *out.input_);
  }
  if (has(node, "output")) {
    out.output_.emplace();
    decodeOutput(getChild(node, "output"), *out.output_);
  }
  if (has(node, "export")) {
    out.export_.emplace();
    decodeExport(getChild(node, "export"), *out.export_);
  }
  if (has(node, "timeout")) {
    const YAML::Node timeout = getChild(node, "timeout");
    if (timeout.IsScalar()) {
      out.timeout_ = timeout.as<std::string>();
    } else {
      Timeout value;
      decodeTimeout(timeout, value);
      out.timeout_ = std::move(value);
    }
  }
  decodeOptional(node, "then", out.then_);
  if (has(node, "metadata")) {
    decodeValue(getChild(node, "metadata"), out.metadata_);
  }

  // The `do` key is overloaded: it is the do task kind, but also a sibling
  // collection of the for task. When `for` is present, `do` belongs to it.
  std::vector<std::string_view> present;
  for (const std::string_view kind : {"call", "do", "emit", "for", "fork", "listen", "raise", "run",
                                      "set", "switch", "try", "wait"}) {
    if (has(node, kind)) {
      present.push_back(kind);
    }
  }
  if (has(node, "for")) {
    present.erase(std::remove(present.begin(), present.end(), "do"), present.end());
  }

  if (present.empty()) {
    fail(node.Mark(),
         "task must declare exactly one task kind (call, do, emit, for, fork, listen, raise, "
         "run, set, switch, try, wait)");
  }
  if (present.size() > 1) {
    fail(node.Mark(), "task declares mutually exclusive kinds '" + std::string(present[0]) +
                          "' and '" + std::string(present[1]) + "'");
  }

  const std::string_view kind = present.front();
  if (kind == "call") {
    CallBody body;
    decodeCallBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "do") {
    DoBody body;
    decodeDoBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "emit") {
    EmitBody body;
    decodeEmitBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "for") {
    ForBody body;
    decodeForBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "fork") {
    ForkBody body;
    decodeForkBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "listen") {
    ListenBody body;
    decodeListenBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "raise") {
    RaiseBody body;
    decodeRaiseBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "run") {
    RunBody body;
    decodeRunBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "set") {
    SetBody body;
    decodeSetBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "switch") {
    SwitchBody body;
    decodeSwitchBody(node, body);
    out.body_ = std::move(body);
  } else if (kind == "try") {
    TryBody body;
    decodeTryBody(node, body);
    out.body_ = std::move(body);
  } else {
    WaitBody body;
    decodeWaitBody(node, body);
    out.body_ = std::move(body);
  }
}

void decodeTasks(const YAML::Node& node, Tasks& out) {
  out.clear();
  if (node.IsSequence()) {
    for (const auto& item : node) {
      requireMap(item, "task collection");
      if (item.size() != 1) {
        fail(item.Mark(), "each task entry must be a single-key mapping");
      }
      const auto entry = *item.begin();
      NamedTask named_task;
      named_task.name_ = entry.first.Scalar();
      decodeTask(entry.second, named_task.task_);
      out.push_back(std::move(named_task));
    }
    return;
  }
  if (node.IsMap()) {
    for (const auto& entry : node) {
      NamedTask named_task;
      named_task.name_ = entry.first.Scalar();
      decodeTask(entry.second, named_task.task_);
      out.push_back(std::move(named_task));
    }
    return;
  }
  fail(node.Mark(), "task collection must be a sequence or a mapping");
}

// ---------------------------------------------------------------------------
// Reusable components and the document
// ---------------------------------------------------------------------------

void decodeExtension(const YAML::Node& node, Extension& out) {
  requireMap(node, "extension");
  rejectUnknownKeys(node, {"extend", "when", "before", "after"}, "extension");
  out.extend_ = decodeRequired<std::string>(node, "extend", "extension");
  decodeOptional(node, "when", out.when_);
  if (has(node, "before")) {
    decodeTasks(getChild(node, "before"), out.before_);
  }
  if (has(node, "after")) {
    decodeTasks(getChild(node, "after"), out.after_);
  }
}

void decodeUse(const YAML::Node& node, Use& out) {
  requireMap(node, "use");
  rejectUnknownKeys(node,
                    {"authentications", "errors", "extensions", "functions", "retries", "secrets",
                     "timeouts", "catalogs"},
                    "use");
  if (has(node, "authentications")) {
    const YAML::Node authentications = getChild(node, "authentications");
    requireMap(authentications, "use.authentications");
    for (const auto& entry : authentications) {
      Authentication value;
      decodeAuthentication(entry.second, value);
      out.authentications_.emplace(entry.first.Scalar(), std::move(value));
    }
  }
  if (has(node, "errors")) {
    const YAML::Node errors = getChild(node, "errors");
    requireMap(errors, "use.errors");
    for (const auto& entry : errors) {
      Error value;
      decodeError(entry.second, value);
      out.errors_.emplace(entry.first.Scalar(), std::move(value));
    }
  }
  if (has(node, "extensions")) {
    const YAML::Node extensions = getChild(node, "extensions");
    requireSequence(extensions, "use.extensions");
    for (const auto& item : extensions) {
      requireMap(item, "use.extensions");
      if (item.size() != 1) {
        fail(item.Mark(), "each extension must be a single-key mapping");
      }
      const auto entry = *item.begin();
      NamedExtension named_extension;
      named_extension.name_ = entry.first.Scalar();
      decodeExtension(entry.second, named_extension.extension_);
      out.extensions_.push_back(std::move(named_extension));
    }
  }
  if (has(node, "functions")) {
    const YAML::Node functions = getChild(node, "functions");
    requireMap(functions, "use.functions");
    for (const auto& entry : functions) {
      Task value;
      decodeTask(entry.second, value);
      out.functions_.emplace(entry.first.Scalar(), std::move(value));
    }
  }
  if (has(node, "retries")) {
    const YAML::Node retries = getChild(node, "retries");
    requireMap(retries, "use.retries");
    for (const auto& entry : retries) {
      RetryPolicy value;
      decodeRetryPolicy(entry.second, value);
      out.retries_.emplace(entry.first.Scalar(), std::move(value));
    }
  }
  decodeStringList(node, "secrets", out.secrets_);
  if (has(node, "timeouts")) {
    const YAML::Node timeouts = getChild(node, "timeouts");
    requireMap(timeouts, "use.timeouts");
    for (const auto& entry : timeouts) {
      Timeout value;
      decodeTimeout(entry.second, value);
      out.timeouts_.emplace(entry.first.Scalar(), std::move(value));
    }
  }
  if (has(node, "catalogs")) {
    const YAML::Node catalogs = getChild(node, "catalogs");
    requireMap(catalogs, "use.catalogs");
    for (const auto& entry : catalogs) {
      Catalog value;
      decodeCatalog(entry.second, value);
      out.catalogs_.emplace(entry.first.Scalar(), std::move(value));
    }
  }
}

void decodeDocumentInfo(const YAML::Node& node, DocumentInfo& out) {
  requireMap(node, "document");
  rejectUnknownKeys(node,
                    {"dsl", "namespace", "name", "version", "title", "summary", "tags", "metadata"},
                    "document");
  out.dsl_ = decodeRequired<std::string>(node, "dsl", "document");
  out.namespace_ = decodeRequired<std::string>(node, "namespace", "document");
  out.name_ = decodeRequired<std::string>(node, "name", "document");
  out.version_ = decodeRequired<std::string>(node, "version", "document");
  decodeOptional(node, "title", out.title_);
  decodeOptional(node, "summary", out.summary_);
  if (has(node, "tags")) {
    decodeValue(getChild(node, "tags"), out.tags_);
  }
  if (has(node, "metadata")) {
    decodeValue(getChild(node, "metadata"), out.metadata_);
  }
}

void decodeSchedule(const YAML::Node& node, Schedule& out) {
  requireMap(node, "schedule");
  rejectUnknownKeys(node, {"every", "cron", "after", "on", "read"}, "schedule");
  if (has(node, "every")) {
    decodeDuration(getChild(node, "every"), out.every_.emplace());
  }
  decodeOptional(node, "cron", out.cron_);
  if (has(node, "after")) {
    decodeDuration(getChild(node, "after"), out.after_.emplace());
  }
  if (has(node, "on")) {
    out.on_.emplace();
    decodeEventConsumptionStrategy(getChild(node, "on"), *out.on_);
  }
  decodeOptional(node, "read", out.read_);
}

void decodeEvaluate(const YAML::Node& node, Evaluate& out) {
  requireMap(node, "evaluate");
  rejectUnknownKeys(node, {"language", "mode"}, "evaluate");
  decodeOptional(node, "language", out.language_);
  decodeOptional(node, "mode", out.mode_);
}

void decodeDocumentImpl(const YAML::Node& node, Document& out) {
  requireMap(node, "workflow");
  rejectUnknownKeys(node,
                    {"document", "input", "use", "do", "timeout", "output", "schedule", "evaluate"},
                    "workflow");
  requireKey(node, "document", "workflow");
  decodeDocumentInfo(getChild(node, "document"), out.document_);
  if (has(node, "input")) {
    out.input_.emplace();
    decodeInput(getChild(node, "input"), *out.input_);
  }
  if (has(node, "use")) {
    out.use_.emplace();
    decodeUse(getChild(node, "use"), *out.use_);
  }
  requireKey(node, "do", "workflow");
  decodeTasks(getChild(node, "do"), out.do_);
  if (has(node, "timeout")) {
    const YAML::Node timeout = getChild(node, "timeout");
    if (timeout.IsScalar()) {
      out.timeout_ = timeout.as<std::string>();
    } else {
      Timeout value;
      decodeTimeout(timeout, value);
      out.timeout_ = std::move(value);
    }
  }
  if (has(node, "output")) {
    out.output_.emplace();
    decodeOutput(getChild(node, "output"), *out.output_);
  }
  if (has(node, "schedule")) {
    out.schedule_.emplace();
    decodeSchedule(getChild(node, "schedule"), *out.schedule_);
  }
  if (has(node, "evaluate")) {
    out.evaluate_.emplace();
    decodeEvaluate(getChild(node, "evaluate"), *out.evaluate_);
  }
}

} // namespace

auto DecodeDocument(const YAML::Node& node, Document& out) -> bool {
  decodeDocumentImpl(node, out);
  return true;
}

} // namespace strij::openworkflow::parser

namespace YAML {

auto convert<strij::openworkflow::Document>::decode(const YAML::Node& node,
                                                    strij::openworkflow::Document& rhs) -> bool {
  return strij::openworkflow::parser::DecodeDocument(node, rhs);
}

} // namespace YAML
