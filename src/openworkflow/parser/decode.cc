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
auto decodeOptional(const YAML::Node& node, std::string_view key) -> std::optional<T> {
  const YAML::Node child = getChild(node, key);
  if (child.IsDefined() && !child.IsNull()) {
    return child.as<T>();
  }

  return std::nullopt;
}

auto decodeStringList(const YAML::Node& node, std::string_view key) -> std::vector<std::string> {
  std::vector<std::string> out;
  const YAML::Node child = getChild(node, key);
  if (child.IsDefined() && !child.IsNull()) {
    requireSequence(child, std::string(key));
    for (const auto& item : child) {
      out.push_back(item.as<std::string>());
    }
  }

  return out;
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

auto decodeValue(const YAML::Node& node) -> Value;
auto decodeDurationUnits(const YAML::Node& node) -> DurationUnits;
auto decodeDuration(const YAML::Node& node) -> Duration;
auto decodeOAuth2Token(const YAML::Node& node) -> OAuth2Token;
auto decodeOAuth2Client(const YAML::Node& node) -> OAuth2AuthenticationProperties::Client;
auto decodeOAuth2Request(const YAML::Node& node) -> OAuth2AuthenticationProperties::Request;
auto decodeOAuth2Endpoints(const YAML::Node& node) -> OAuth2AuthenticationProperties::Endpoints;
auto decodeOAuth2Properties(const YAML::Node& node) -> OAuth2AuthenticationProperties;
auto decodeBasicAuthentication(const YAML::Node& node) -> BasicAuthentication;
auto decodeBearerAuthentication(const YAML::Node& node) -> BearerAuthentication;
auto decodeDigestAuthentication(const YAML::Node& node) -> DigestAuthentication;
auto decodeOAuth2Authentication(const YAML::Node& node) -> OAuth2Authentication;
auto decodeOidcAuthentication(const YAML::Node& node) -> OidcAuthentication;
auto decodeAuthentication(const YAML::Node& node) -> Authentication;
auto decodeEndpointObject(const YAML::Node& node) -> EndpointObject;
auto decodeEndpoint(const YAML::Node& node) -> Endpoint;
auto decodeExternalResource(const YAML::Node& node) -> ExternalResource;
auto decodeSchema(const YAML::Node& node) -> Schema;
auto decodeInput(const YAML::Node& node) -> Input;
auto decodeOutput(const YAML::Node& node) -> Output;
auto decodeExport(const YAML::Node& node) -> Export;
auto decodeError(const YAML::Node& node) -> Error;
auto decodeErrorFilter(const YAML::Node& node) -> ErrorFilter;
auto decodeBackoff(const YAML::Node& node) -> Backoff;
auto decodeJitter(const YAML::Node& node) -> Jitter;
auto decodeRetryAttempt(const YAML::Node& node) -> RetryLimit::Attempt;
auto decodeRetryLimit(const YAML::Node& node) -> RetryLimit;
auto decodeRetryPolicy(const YAML::Node& node) -> RetryPolicy;
auto decodeCatalog(const YAML::Node& node) -> Catalog;
auto decodeTimeout(const YAML::Node& node) -> Timeout;
auto decodeEventProperties(const YAML::Node& node) -> EventProperties;
auto decodeCorrelation(const YAML::Node& node) -> Correlation;
auto decodeEventFilter(const YAML::Node& node) -> EventFilter;
auto decodeEventFilterList(const YAML::Node& node) -> std::vector<EventFilter>;
auto decodeEventConsumptionStrategy(const YAML::Node& node) -> EventConsumptionStrategy;
auto decodeSubscriptionIterator(const YAML::Node& node) -> SubscriptionIterator;
auto decodeContainerLifetime(const YAML::Node& node) -> ContainerLifetime;
auto decodeContainerProcess(const YAML::Node& node) -> ContainerProcess;
auto decodeScriptProcess(const YAML::Node& node) -> ScriptProcess;
auto decodeShellProcess(const YAML::Node& node) -> ShellProcess;
auto decodeWorkflowProcess(const YAML::Node& node) -> WorkflowProcess;
auto decodeCallBody(const YAML::Node& node) -> CallBody;
auto decodeDoBody(const YAML::Node& node) -> DoBody;
auto decodeEmitBody(const YAML::Node& node) -> EmitBody;
auto decodeEvent(const YAML::Node& node) -> Event;
auto decodeForSpec(const YAML::Node& node) -> ForSpec;
auto decodeForBody(const YAML::Node& node) -> ForBody;
auto decodeForkBody(const YAML::Node& node) -> ForkBody;
auto decodeListenBody(const YAML::Node& node) -> ListenBody;
auto decodeRaiseBody(const YAML::Node& node) -> RaiseBody;
auto decodeRunBody(const YAML::Node& node) -> RunBody;
auto decodeSetBody(const YAML::Node& node) -> SetBody;
auto decodeSwitchBody(const YAML::Node& node) -> SwitchBody;
auto decodeCatch(const YAML::Node& node) -> Catch;
auto decodeTryBody(const YAML::Node& node) -> TryBody;
auto decodeWaitBody(const YAML::Node& node) -> WaitBody;
auto decodeTask(const YAML::Node& node) -> Task;
auto decodeTasks(const YAML::Node& node) -> Tasks;
auto decodeExtension(const YAML::Node& node) -> Extension;
auto decodeUse(const YAML::Node& node) -> Use;
auto decodeDocumentInfo(const YAML::Node& node) -> DocumentInfo;
auto decodeSchedule(const YAML::Node& node) -> Schedule;
auto decodeEvaluate(const YAML::Node& node) -> Evaluate;

// ---------------------------------------------------------------------------
// Leaf and shared shapes
// ---------------------------------------------------------------------------

auto decodeValue(const YAML::Node& node) -> Value {
  if (!node.IsDefined() || node.IsNull()) {
    return {};
  }

  if (node.IsSequence()) {
    Value::Array array{};
    array.reserve(node.size());
    for (const auto& item : node) {
      array.push_back(decodeValue(item));
    }

    return std::move(array);
  }

  if (node.IsMap()) {
    Value::Object object{};

    for (const auto& entry : node) {
      object.emplace(entry.first.Scalar(), decodeValue(entry.second));
    }

    return std::move(object);
  }

  bool boolean_value{false};
  if (YAML::convert<bool>::decode(node, boolean_value)) {
    return boolean_value;
  }

  std::int64_t integer_value{0};
  if (YAML::convert<std::int64_t>::decode(node, integer_value)) {
    return integer_value;
  }

  double double_value{0.0};
  if (YAML::convert<double>::decode(node, double_value)) {
    return double_value;
  }

  return node.Scalar();
}

auto decodeDurationUnits(const YAML::Node& node) -> DurationUnits {
  requireMap(node, "duration");
  rejectUnknownKeys(node, {"days", "hours", "minutes", "seconds", "milliseconds"}, "duration");
  return {.days_ = decodeOptional<std::int64_t>(node, "days"),
          .hours_ = decodeOptional<std::int64_t>(node, "hours"),
          .minutes_ = decodeOptional<std::int64_t>(node, "minutes"),
          .seconds_ = decodeOptional<std::int64_t>(node, "seconds"),
          .milliseconds_ = decodeOptional<std::int64_t>(node, "milliseconds")};
}

auto decodeDuration(const YAML::Node& node) -> Duration {
  if (node.IsScalar()) {
    return node.as<std::string>();
  }

  if (node.IsMap()) {
    return decodeDurationUnits(node);
  }

  fail(node.Mark(), "duration must be a string or a mapping");
}

auto decodeOAuth2Token(const YAML::Node& node) -> OAuth2Token {
  requireMap(node, "oauth2 token");
  rejectUnknownKeys(node, {"token", "type"}, "oauth2 token");
  return {.token_ = decodeRequired<std::string>(node, "token", "oauth2 token"),
          .type_ = decodeRequired<std::string>(node, "type", "oauth2 token")};
}

auto decodeOAuth2Client(const YAML::Node& node) -> OAuth2AuthenticationProperties::Client {
  requireMap(node, "oauth2 client");
  rejectUnknownKeys(node, {"id", "secret", "assertion", "authentication"}, "oauth2 client");
  return {.id_ = decodeOptional<std::string>(node, "id"),
          .secret_ = decodeOptional<std::string>(node, "secret"),
          .assertion_ = decodeOptional<std::string>(node, "assertion"),
          .authentication_ = decodeOptional<std::string>(node, "authentication")};
}

auto decodeOAuth2Request(const YAML::Node& node) -> OAuth2AuthenticationProperties::Request {
  requireMap(node, "oauth2 request");
  rejectUnknownKeys(node, {"encoding"}, "oauth2 request");
  return {.encoding_ = decodeOptional<std::string>(node, "encoding")};
}

auto decodeOAuth2Endpoints(const YAML::Node& node) -> OAuth2AuthenticationProperties::Endpoints {
  requireMap(node, "oauth2 endpoints");
  rejectUnknownKeys(node, {"token", "introspection"}, "oauth2 endpoints");
  return {.token_ = decodeOptional<std::string>(node, "token"),
          .introspection_ = decodeOptional<std::string>(node, "introspection")};
}

auto decodeOAuth2Properties(const YAML::Node& node) -> OAuth2AuthenticationProperties {
  requireMap(node, "oauth2 authentication");
  rejectUnknownKeys(node,
                    {"authority", "grant", "client", "request", "endpoints", "issuers", "scopes",
                     "audiences", "username", "password", "subject", "actor"},
                    "oauth2 authentication");

  OAuth2AuthenticationProperties out{
      .authority_ = decodeRequired<std::string>(node, "authority", "oauth2 authentication"),
      .grant_ = decodeRequired<std::string>(node, "grant", "oauth2 authentication"),
      .issuers_ = decodeStringList(node, "issuers"),
      .scopes_ = decodeStringList(node, "scopes"),
      .audiences_ = decodeStringList(node, "audiences"),
      .username_ = decodeOptional<std::string>(node, "username"),
      .password_ = decodeOptional<std::string>(node, "password")};

  if (has(node, "client")) {
    out.client_ = decodeOAuth2Client(getChild(node, "client"));
  }

  if (has(node, "request")) {
    out.request_ = decodeOAuth2Request(getChild(node, "request"));
  }

  if (has(node, "endpoints")) {
    out.endpoints_ = decodeOAuth2Endpoints(getChild(node, "endpoints"));
  }

  if (has(node, "subject")) {
    out.subject_ = decodeOAuth2Token(getChild(node, "subject"));
  }

  if (has(node, "actor")) {
    out.actor_ = decodeOAuth2Token(getChild(node, "actor"));
  }

  return out;
}

auto decodeBasicAuthentication(const YAML::Node& node) -> BasicAuthentication {
  BasicAuthentication out;
  requireMap(node, "basic authentication");

  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "basic authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "basic authentication reference");

    return out;
  }

  rejectUnknownKeys(node, {"username", "password"}, "basic authentication");
  out.username_ = decodeRequired<std::string>(node, "username", "basic authentication");
  out.password_ = decodeRequired<std::string>(node, "password", "basic authentication");

  return out;
}

auto decodeBearerAuthentication(const YAML::Node& node) -> BearerAuthentication {
  BearerAuthentication out;
  requireMap(node, "bearer authentication");

  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "bearer authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "bearer authentication reference");

    return out;
  }

  rejectUnknownKeys(node, {"token"}, "bearer authentication");
  out.token_ = decodeRequired<std::string>(node, "token", "bearer authentication");

  return out;
}

auto decodeDigestAuthentication(const YAML::Node& node) -> DigestAuthentication {
  DigestAuthentication out;
  requireMap(node, "digest authentication");

  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "digest authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "digest authentication reference");

    return out;
  }

  rejectUnknownKeys(node, {"username", "password"}, "digest authentication");
  out.username_ = decodeRequired<std::string>(node, "username", "digest authentication");
  out.password_ = decodeRequired<std::string>(node, "password", "digest authentication");

  return out;
}

auto decodeOAuth2Authentication(const YAML::Node& node) -> OAuth2Authentication {
  OAuth2Authentication out;
  requireMap(node, "oauth2 authentication");

  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "oauth2 authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "oauth2 authentication reference");

    return out;
  }

  out.properties_ = decodeOAuth2Properties(node);

  return out;
}

auto decodeOidcAuthentication(const YAML::Node& node) -> OidcAuthentication {
  OidcAuthentication out;
  requireMap(node, "oidc authentication");

  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "oidc authentication reference");
    out.use_ = decodeRequired<std::string>(node, "use", "oidc authentication reference");

    return out;
  }

  out.properties_ = decodeOAuth2Properties(node);

  return out;
}

auto decodeAuthentication(const YAML::Node& node) -> Authentication {
  Authentication out;
  requireMap(node, "authentication");

  if (has(node, "use")) {
    rejectUnknownKeys(node, {"use"}, "authentication");
    out.scheme_ =
        AuthenticationReference{decodeRequired<std::string>(node, "use", "authentication")};

    return out;
  }

  const std::string_view scheme =
      SelectOne(node, {"basic", "bearer", "digest", "oauth2", "oidc"}, "authentication scheme");
  rejectUnknownKeys(node, {"basic", "bearer", "digest", "oauth2", "oidc"}, "authentication");
  const YAML::Node scheme_node = getChild(node, scheme);

  if (scheme == "basic") {
    out.scheme_ = decodeBasicAuthentication(scheme_node);
  } else if (scheme == "bearer") {
    out.scheme_ = decodeBearerAuthentication(scheme_node);
  } else if (scheme == "digest") {
    out.scheme_ = decodeDigestAuthentication(scheme_node);
  } else if (scheme == "oauth2") {
    out.scheme_ = decodeOAuth2Authentication(scheme_node);
  } else {
    out.scheme_ = decodeOidcAuthentication(scheme_node);
  }

  return out;
}

auto decodeEndpointObject(const YAML::Node& node) -> EndpointObject {
  requireMap(node, "endpoint");
  rejectUnknownKeys(node, {"uri", "authentication"}, "endpoint");
  EndpointObject out{.uri_ = decodeRequired<std::string>(node, "uri", "endpoint")};

  if (has(node, "authentication")) {
    out.authentication_ = decodeAuthentication(getChild(node, "authentication"));
  }

  return out;
}

auto decodeEndpoint(const YAML::Node& node) -> Endpoint {
  Endpoint out;

  if (node.IsScalar()) {
    out.value_ = node.as<std::string>();

    return out;
  }

  if (node.IsMap()) {
    out.value_ = decodeEndpointObject(node);

    return out;
  }

  fail(node.Mark(), "endpoint must be a string or a mapping");
}

auto decodeExternalResource(const YAML::Node& node) -> ExternalResource {
  requireMap(node, "external resource");
  rejectUnknownKeys(node, {"name", "endpoint"}, "external resource");
  requireKey(node, "endpoint", "external resource");
  return {.name_ = decodeOptional<std::string>(node, "name"),
          .endpoint_ = decodeEndpoint(getChild(node, "endpoint"))};
}

auto decodeSchema(const YAML::Node& node) -> Schema {
  requireMap(node, "schema");
  rejectUnknownKeys(node, {"format", "document", "resource"}, "schema");
  const std::string_view source = SelectOne(node, {"document", "resource"}, "schema source");
  const YAML::Node source_node = getChild(node, source);

  Schema out{.format_ = decodeOptional<std::string>(node, "format").value_or("json")};

  if (source == "document") {
    out.source_ = decodeValue(source_node);
  } else {
    out.source_ = decodeExternalResource(source_node);
  }

  return out;
}

auto decodeInput(const YAML::Node& node) -> Input {
  Input out;
  requireMap(node, "input");
  rejectUnknownKeys(node, {"schema", "from"}, "input");

  if (has(node, "schema")) {
    out.schema_ = decodeSchema(getChild(node, "schema"));
  }

  if (has(node, "from")) {
    const YAML::Node child = getChild(node, "from");
    if (child.IsScalar()) {
      out.from_ = child.as<std::string>();
    } else {
      out.from_ = decodeValue(child);
    }
  }

  return out;
}

auto decodeOutput(const YAML::Node& node) -> Output {
  Output out;
  requireMap(node, "output");
  rejectUnknownKeys(node, {"schema", "as"}, "output");

  if (has(node, "schema")) {
    out.schema_ = decodeSchema(getChild(node, "schema"));
  }

  if (has(node, "as")) {
    const YAML::Node child = getChild(node, "as");
    if (child.IsScalar()) {
      out.as_ = child.as<std::string>();
    } else {
      out.as_ = decodeValue(child);
    }
  }

  return out;
}

auto decodeExport(const YAML::Node& node) -> Export {
  Export out;
  requireMap(node, "export");
  rejectUnknownKeys(node, {"schema", "as"}, "export");

  if (has(node, "schema")) {
    out.schema_ = decodeSchema(getChild(node, "schema"));
  }

  if (has(node, "as")) {
    const YAML::Node child = getChild(node, "as");
    if (child.IsScalar()) {
      out.as_ = child.as<std::string>();
    } else {
      out.as_ = decodeValue(child);
    }
  }

  return out;
}

auto decodeError(const YAML::Node& node) -> Error {
  requireMap(node, "error");
  rejectUnknownKeys(node, {"type", "status", "instance", "title", "detail"}, "error");
  return {.type_ = decodeRequired<std::string>(node, "type", "error"),
          .status_ = decodeRequired<std::int64_t>(node, "status", "error"),
          .instance_ = decodeOptional<std::string>(node, "instance"),
          .title_ = decodeOptional<std::string>(node, "title"),
          .detail_ = decodeOptional<std::string>(node, "detail")};
}

auto decodeErrorFilter(const YAML::Node& node) -> ErrorFilter {
  requireMap(node, "error filter");
  rejectUnknownKeys(node, {"type", "status", "instance", "title", "detail"}, "error filter");
  return {.type_ = decodeOptional<std::string>(node, "type"),
          .status_ = decodeOptional<std::int64_t>(node, "status"),
          .instance_ = decodeOptional<std::string>(node, "instance"),
          .title_ = decodeOptional<std::string>(node, "title"),
          .detail_ = decodeOptional<std::string>(node, "detail")};
}

auto decodeBackoff(const YAML::Node& node) -> Backoff {
  requireMap(node, "retry backoff");
  const std::string_view kind =
      SelectOne(node, {"constant", "exponential", "linear"}, "retry backoff branch");
  rejectUnknownKeys(node, {"constant", "exponential", "linear"}, "retry backoff");
  return {.kind_ = std::string(kind), .parameters_ = decodeValue(getChild(node, kind))};
}

auto decodeJitter(const YAML::Node& node) -> Jitter {
  requireMap(node, "retry jitter");
  rejectUnknownKeys(node, {"from", "to"}, "retry jitter");
  requireKey(node, "from", "retry jitter");
  requireKey(node, "to", "retry jitter");
  return {.from_ = decodeDuration(getChild(node, "from")),
          .to_ = decodeDuration(getChild(node, "to"))};
}

auto decodeRetryAttempt(const YAML::Node& node) -> RetryLimit::Attempt {
  RetryLimit::Attempt out;
  requireMap(node, "retry attempt");
  rejectUnknownKeys(node, {"count", "duration"}, "retry attempt");
  out.count_ = decodeOptional<std::int64_t>(node, "count");

  if (has(node, "duration")) {
    out.duration_ = decodeDuration(getChild(node, "duration"));
  }

  return out;
}

auto decodeRetryLimit(const YAML::Node& node) -> RetryLimit {
  RetryLimit out;
  requireMap(node, "retry limit");
  rejectUnknownKeys(node, {"attempt", "duration"}, "retry limit");

  if (has(node, "attempt")) {
    out.attempt_ = decodeRetryAttempt(getChild(node, "attempt"));
  }

  if (has(node, "duration")) {
    out.duration_ = decodeDuration(getChild(node, "duration"));
  }

  return out;
}

auto decodeRetryPolicy(const YAML::Node& node) -> RetryPolicy {
  RetryPolicy out;
  requireMap(node, "retry policy");
  rejectUnknownKeys(node, {"when", "exceptWhen", "delay", "backoff", "limit", "jitter"},
                    "retry policy");

  out.when_ = decodeOptional<std::string>(node, "when");
  out.exceptWhen_ = decodeOptional<std::string>(node, "exceptWhen");

  if (has(node, "delay")) {
    out.delay_ = decodeDuration(getChild(node, "delay"));
  }

  if (has(node, "backoff")) {
    out.backoff_ = decodeBackoff(getChild(node, "backoff"));
  }

  if (has(node, "limit")) {
    out.limit_ = decodeRetryLimit(getChild(node, "limit"));
  }

  if (has(node, "jitter")) {
    out.jitter_ = decodeJitter(getChild(node, "jitter"));
  }

  return out;
}

auto decodeCatalog(const YAML::Node& node) -> Catalog {
  requireMap(node, "catalog");
  rejectUnknownKeys(node, {"endpoint"}, "catalog");
  requireKey(node, "endpoint", "catalog");
  return {.endpoint_ = decodeEndpoint(getChild(node, "endpoint"))};
}

auto decodeTimeout(const YAML::Node& node) -> Timeout {
  requireMap(node, "timeout");
  rejectUnknownKeys(node, {"after"}, "timeout");
  requireKey(node, "after", "timeout");
  return {.after_ = decodeDuration(getChild(node, "after"))};
}

// ---------------------------------------------------------------------------
// Event machinery
// ---------------------------------------------------------------------------

auto decodeEventProperties(const YAML::Node& node) -> EventProperties {
  requireMap(node, "event properties");
  rejectUnknownKeys(
      node, {"id", "source", "type", "time", "subject", "datacontenttype", "dataschema", "data"},
      "event properties");

  EventProperties out{.id_ = decodeOptional<std::string>(node, "id"),
                      .source_ = decodeOptional<std::string>(node, "source"),
                      .type_ = decodeOptional<std::string>(node, "type"),
                      .time_ = decodeOptional<std::string>(node, "time"),
                      .subject_ = decodeOptional<std::string>(node, "subject"),
                      .datacontenttype_ = decodeOptional<std::string>(node, "datacontenttype"),
                      .dataschema_ = decodeOptional<std::string>(node, "dataschema")};

  if (has(node, "data")) {
    out.data_ = decodeValue(getChild(node, "data"));
  }

  return out;
}

auto decodeCorrelation(const YAML::Node& node) -> Correlation {
  requireMap(node, "correlation");
  rejectUnknownKeys(node, {"from", "expect"}, "correlation");
  return {.from_ = decodeRequired<std::string>(node, "from", "correlation"),
          .expect_ = decodeOptional<std::string>(node, "expect")};
}

auto decodeEventFilter(const YAML::Node& node) -> EventFilter {
  EventFilter out;
  requireMap(node, "event filter");
  rejectUnknownKeys(node, {"with", "correlate"}, "event filter");
  requireKey(node, "with", "event filter");

  out.with_ = decodeEventProperties(getChild(node, "with"));

  if (has(node, "correlate")) {
    const YAML::Node correlate = getChild(node, "correlate");
    requireMap(correlate, "event correlation");
    for (const auto& entry : correlate) {
      out.correlate_.emplace(entry.first.Scalar(), decodeCorrelation(entry.second));
    }
  }

  return out;
}

auto decodeEventFilterList(const YAML::Node& node) -> std::vector<EventFilter> {
  std::vector<EventFilter> out;
  requireSequence(node, "event list");

  for (const auto& item : node) {
    out.push_back(decodeEventFilter(item));
  }

  return out;
}

auto decodeEventConsumptionStrategy(const YAML::Node& node) -> EventConsumptionStrategy {
  EventConsumptionStrategy out;
  requireMap(node, "event consumption strategy");
  const std::string_view mode =
      SelectOne(node, {"all", "any", "one"}, "event consumption strategy");
  rejectUnknownKeys(node, {"all", "any", "one", "until"}, "event consumption strategy");

  if (mode == "all") {
    out.mode_ = EventConsumptionStrategy::Mode::kAll;
    out.filters_ = decodeEventFilterList(getChild(node, "all"));
  } else if (mode == "any") {
    out.mode_ = EventConsumptionStrategy::Mode::kAny;
    out.filters_ = decodeEventFilterList(getChild(node, "any"));
    if (has(node, "until")) {
      out.until_ = decodeValue(getChild(node, "until"));
    }
  } else {
    out.mode_ = EventConsumptionStrategy::Mode::kOne;
    out.filters_.push_back(decodeEventFilter(getChild(node, "one")));
  }

  return out;
}

auto decodeSubscriptionIterator(const YAML::Node& node) -> SubscriptionIterator {
  SubscriptionIterator out;

  requireMap(node, "subscription iterator");
  rejectUnknownKeys(node, {"item", "at", "do", "output", "export"}, "subscription iterator");

  out.item_ = decodeOptional<std::string>(node, "item");
  out.at_ = decodeOptional<std::string>(node, "at");

  if (has(node, "do")) {
    out.do_ = decodeTasks(getChild(node, "do"));
  }

  if (has(node, "output")) {
    out.output_ = decodeOutput(getChild(node, "output"));
  }

  if (has(node, "export")) {
    out.export_ = decodeExport(getChild(node, "export"));
  }

  return out;
}

// ---------------------------------------------------------------------------
// Task bodies
// ---------------------------------------------------------------------------

auto decodeContainerLifetime(const YAML::Node& node) -> ContainerLifetime {
  requireMap(node, "container lifetime");
  rejectUnknownKeys(node, {"cleanup", "after"}, "container lifetime");

  ContainerLifetime out{.cleanup_ =
                            decodeRequired<std::string>(node, "cleanup", "container lifetime")};

  if (has(node, "after")) {
    out.after_ = decodeDuration(getChild(node, "after"));
  }

  return out;
}

auto decodeContainerProcess(const YAML::Node& node) -> ContainerProcess {
  requireMap(node, "container process");
  rejectUnknownKeys(node,
                    {"image", "name", "command", "ports", "volumes", "environment", "stdin",
                     "arguments", "lifetime", "pullPolicy"},
                    "container process");

  ContainerProcess out{.image_ = decodeRequired<std::string>(node, "image", "container process"),
                       .name_ = decodeOptional<std::string>(node, "name"),
                       .command_ = decodeOptional<std::string>(node, "command"),
                       .stdin_ = decodeOptional<std::string>(node, "stdin"),
                       .arguments_ = decodeStringList(node, "arguments"),
                       .pull_policy_ = decodeOptional<std::string>(node, "pullPolicy")};

  if (has(node, "ports")) {
    out.ports_ = decodeValue(getChild(node, "ports"));
  }

  if (has(node, "volumes")) {
    out.volumes_ = decodeValue(getChild(node, "volumes"));
  }

  if (has(node, "environment")) {
    out.environment_ = decodeValue(getChild(node, "environment"));
  }

  if (has(node, "lifetime")) {
    out.lifetime_ = decodeContainerLifetime(getChild(node, "lifetime"));
  }

  return out;
}

auto decodeScriptProcess(const YAML::Node& node) -> ScriptProcess {
  requireMap(node, "script process");
  const std::string_view source = SelectOne(node, {"code", "resource"}, "script process source");
  rejectUnknownKeys(node, {"language", "stdin", "arguments", "environment", "code", "resource"},
                    "script process");

  ScriptProcess out{.language_ = decodeRequired<std::string>(node, "language", "script process"),
                    .stdin_ = decodeOptional<std::string>(node, "stdin"),
                    .arguments_ = decodeStringList(node, "arguments")};

  if (has(node, "environment")) {
    out.environment_ = decodeValue(getChild(node, "environment"));
  }

  if (source == "code") {
    out.source_ = ScriptSourceCode{decodeRequired<std::string>(node, "code", "script process")};
  } else {
    out.source_ = ScriptSourceResource{decodeExternalResource(getChild(node, "resource"))};
  }

  return out;
}

auto decodeShellProcess(const YAML::Node& node) -> ShellProcess {
  requireMap(node, "shell process");
  rejectUnknownKeys(node, {"command", "stdin", "arguments", "environment"}, "shell process");

  ShellProcess out{.command_ = decodeRequired<std::string>(node, "command", "shell process"),
                   .stdin_ = decodeOptional<std::string>(node, "stdin"),
                   .arguments_ = decodeStringList(node, "arguments")};

  if (has(node, "environment")) {
    out.environment_ = decodeValue(getChild(node, "environment"));
  }

  return out;
}

auto decodeWorkflowProcess(const YAML::Node& node) -> WorkflowProcess {
  requireMap(node, "workflow process");
  rejectUnknownKeys(node, {"namespace", "name", "version", "input"}, "workflow process");

  WorkflowProcess out{.namespace_ =
                          decodeRequired<std::string>(node, "namespace", "workflow process"),
                      .name_ = decodeRequired<std::string>(node, "name", "workflow process"),
                      .version_ = decodeRequired<std::string>(node, "version", "workflow process")};

  if (has(node, "input")) {
    out.input_ = decodeValue(getChild(node, "input"));
  }

  return out;
}

auto decodeCallBody(const YAML::Node& node) -> CallBody {
  requireKey(node, "call", "call task");

  CallBody out{.call_ = decodeRequired<std::string>(node, "call", "call task")};

  if (has(node, "with")) {
    out.with_ = decodeValue(getChild(node, "with"));
  }

  return out;
}

auto decodeDoBody(const YAML::Node& node) -> DoBody {
  requireKey(node, "do", "do task");
  return {.do_ = decodeTasks(getChild(node, "do"))};
}

auto decodeEvent(const YAML::Node& node) -> Event {
  requireMap(node, "event");
  rejectUnknownKeys(node, {"with"}, "event");

  Event out;

  if (has(node, "with")) {
    out.with_ = decodeEventProperties(getChild(node, "with"));
  }

  return out;
}

auto decodeEmitBody(const YAML::Node& node) -> EmitBody {
  requireKey(node, "emit", "emit task");
  const YAML::Node emit = getChild(node, "emit");
  requireMap(emit, "emit task");
  rejectUnknownKeys(emit, {"event"}, "emit task");
  requireKey(emit, "event", "emit task");
  return {.event_ = decodeEvent(getChild(emit, "event"))};
}

auto decodeForSpec(const YAML::Node& node) -> ForSpec {
  requireMap(node, "for");
  rejectUnknownKeys(node, {"each", "at", "in"}, "for");
  requireKey(node, "in", "for");

  ForSpec out{.each_ = decodeOptional<std::string>(node, "each"),
              .at_ = decodeOptional<std::string>(node, "at")};

  const YAML::Node in_key = getChild(node, "in");
  if (in_key.IsScalar()) {
    out.in_ = in_key.as<std::string>();
  } else {
    out.in_ = decodeValue(in_key);
  }

  return out;
}

auto decodeForBody(const YAML::Node& node) -> ForBody {
  requireKey(node, "for", "for task");
  requireKey(node, "do", "for task");
  return {.for_ = decodeForSpec(getChild(node, "for")),
          .while_ = decodeOptional<std::string>(node, "while"),
          .do_ = decodeTasks(getChild(node, "do"))};
}

auto decodeForkBody(const YAML::Node& node) -> ForkBody {
  requireKey(node, "fork", "fork task");
  const YAML::Node fork = getChild(node, "fork");
  requireMap(fork, "fork task");
  rejectUnknownKeys(fork, {"branches", "compete"}, "fork task");
  requireKey(fork, "branches", "fork task");
  return {.branches_ = decodeTasks(getChild(fork, "branches")),
          .compete_ = decodeOptional<bool>(fork, "compete").value_or(false)};
}

auto decodeListenBody(const YAML::Node& node) -> ListenBody {
  requireKey(node, "listen", "listen task");
  const YAML::Node listen = getChild(node, "listen");
  requireMap(listen, "listen task");
  rejectUnknownKeys(listen, {"to", "read"}, "listen task");
  requireKey(listen, "to", "listen task");

  ListenBody out{.to_ = decodeEventConsumptionStrategy(getChild(listen, "to")),
                 .read_ = decodeOptional<std::string>(listen, "read")};

  if (has(node, "foreach")) {
    out.foreach_ = decodeSubscriptionIterator(getChild(node, "foreach"));
  }

  return out;
}

auto decodeRaiseBody(const YAML::Node& node) -> RaiseBody {
  requireKey(node, "raise", "raise task");
  const YAML::Node raise = getChild(node, "raise");
  requireMap(raise, "raise task");
  rejectUnknownKeys(raise, {"error"}, "raise task");
  requireKey(raise, "error", "raise task");
  const YAML::Node error = getChild(raise, "error");

  RaiseBody out;

  if (error.IsScalar()) {
    out.error_ = error.as<std::string>();
  } else {
    out.error_ = decodeError(error);
  }

  return out;
}

auto decodeRunBody(const YAML::Node& node) -> RunBody {
  requireKey(node, "run", "run task");
  const YAML::Node run = getChild(node, "run");
  requireMap(run, "run task");
  const std::string_view kind =
      SelectOne(run, {"container", "shell", "script", "workflow"}, "run process kind");
  rejectUnknownKeys(run, {"container", "shell", "script", "workflow", "await", "return"},
                    "run task");
  const YAML::Node process = getChild(run, kind);

  RunBody out{.await_ = decodeOptional<bool>(run, "await"),
              .return_ = decodeOptional<std::string>(run, "return")};

  if (kind == "container") {
    out.process_ = decodeContainerProcess(process);
  } else if (kind == "shell") {
    out.process_ = decodeShellProcess(process);
  } else if (kind == "script") {
    out.process_ = decodeScriptProcess(process);
  } else {
    out.process_ = decodeWorkflowProcess(process);
  }

  return out;
}

auto decodeSetBody(const YAML::Node& node) -> SetBody {
  requireKey(node, "set", "set task");
  const YAML::Node set = getChild(node, "set");

  SetBody out;

  if (set.IsScalar()) {
    out.set_ = set.as<std::string>();
  } else {
    out.set_ = decodeValue(set);
  }

  return out;
}

auto decodeSwitchBody(const YAML::Node& node) -> SwitchBody {
  requireKey(node, "switch", "switch task");
  const YAML::Node node_switch = getChild(node, "switch");
  requireSequence(node_switch, "switch task");

  SwitchBody out;

  for (const auto& item : node_switch) {
    requireMap(item, "switch case");

    if (item.size() != 1) {
      fail(item.Mark(), "each switch case must have exactly one name");
    }

    const auto entry = *item.begin();
    const YAML::Node body = entry.second;
    requireMap(body, "switch case");
    rejectUnknownKeys(body, {"when", "then"}, "switch case");
    out.cases_.push_back({.name_ = entry.first.Scalar(),
                          .when_ = decodeOptional<std::string>(body, "when"),
                          .then_ = decodeOptional<std::string>(body, "then")});
  }

  return out;
}

auto decodeCatch(const YAML::Node& node) -> Catch {
  requireMap(node, "catch");
  rejectUnknownKeys(node, {"errors", "as", "when", "exceptWhen", "retry", "do", "then"}, "catch");

  Catch out{.as_ = decodeOptional<std::string>(node, "as"),
            .when_ = decodeOptional<std::string>(node, "when"),
            .exceptWhen_ = decodeOptional<std::string>(node, "exceptWhen"),
            .then_ = decodeOptional<std::string>(node, "then")};

  if (has(node, "errors")) {
    const YAML::Node errors = getChild(node, "errors");
    requireMap(errors, "catch errors");
    rejectUnknownKeys(errors, {"with"}, "catch errors");

    if (has(errors, "with")) {
      out.errors_ = decodeErrorFilter(getChild(errors, "with"));
    }
  }

  if (has(node, "retry")) {
    const YAML::Node retry = getChild(node, "retry");
    if (retry.IsScalar()) {
      out.retry_ = retry.as<std::string>();
    } else {
      out.retry_ = decodeRetryPolicy(retry);
    }
  }

  if (has(node, "do")) {
    out.do_ = decodeTasks(getChild(node, "do"));
  }

  return out;
}

auto decodeTryBody(const YAML::Node& node) -> TryBody {
  requireKey(node, "try", "try task");

  TryBody out{.try_ = decodeTasks(getChild(node, "try"))};

  if (has(node, "catch")) {
    out.catch_ = decodeCatch(getChild(node, "catch"));
  }

  return out;
}

auto decodeWaitBody(const YAML::Node& node) -> WaitBody {
  requireKey(node, "wait", "wait task");
  return {.wait_ = decodeDuration(getChild(node, "wait"))};
}

// ---------------------------------------------------------------------------
// Task union and collections
// ---------------------------------------------------------------------------

auto decodeTask(const YAML::Node& node) -> Task {
  requireMap(node, "task");
  rejectUnknownKeys(node,
                    {"if",     "input", "output", "export", "timeout", "then",  "metadata", "call",
                     "do",     "emit",  "for",    "fork",   "listen",  "raise", "run",      "set",
                     "switch", "try",   "wait",   "with",   "foreach", "while", "catch"},
                    "task");
  Task out{.if_ = decodeOptional<std::string>(node, "if"),
           .then_ = decodeOptional<std::string>(node, "then")};

  if (has(node, "input")) {
    out.input_ = decodeInput(getChild(node, "input"));
  }

  if (has(node, "output")) {
    out.output_ = decodeOutput(getChild(node, "output"));
  }

  if (has(node, "export")) {
    out.export_ = decodeExport(getChild(node, "export"));
  }

  if (has(node, "timeout")) {
    const YAML::Node timeout = getChild(node, "timeout");

    if (timeout.IsScalar()) {
      out.timeout_ = timeout.as<std::string>();
    } else {
      out.timeout_ = decodeTimeout(timeout);
    }
  }

  if (has(node, "metadata")) {
    out.metadata_ = decodeValue(getChild(node, "metadata"));
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
  } else if (present.size() > 1) {
    fail(node.Mark(), "task declares mutually exclusive kinds '" + std::string(present[0]) +
                          "' and '" + std::string(present[1]) + "'");
  }

  const std::string_view kind = present.front();
  if (kind == "call") {
    out.body_ = decodeCallBody(node);
  } else if (kind == "do") {
    out.body_ = decodeDoBody(node);
  } else if (kind == "emit") {
    out.body_ = decodeEmitBody(node);
  } else if (kind == "for") {
    out.body_ = decodeForBody(node);
  } else if (kind == "fork") {
    out.body_ = decodeForkBody(node);
  } else if (kind == "listen") {
    out.body_ = decodeListenBody(node);
  } else if (kind == "raise") {
    out.body_ = decodeRaiseBody(node);
  } else if (kind == "run") {
    out.body_ = decodeRunBody(node);
  } else if (kind == "set") {
    out.body_ = decodeSetBody(node);
  } else if (kind == "switch") {
    out.body_ = decodeSwitchBody(node);
  } else if (kind == "try") {
    out.body_ = decodeTryBody(node);
  } else {
    out.body_ = decodeWaitBody(node);
  }

  return out;
}

auto decodeTasks(const YAML::Node& node) -> Tasks {
  Tasks out;

  if (node.IsSequence()) {
    for (const auto& item : node) {
      requireMap(item, "task collection");

      if (item.size() != 1) {
        fail(item.Mark(), "each task entry must be a single-key mapping");
      }

      const auto entry = *item.begin();
      out.push_back({.name_ = entry.first.Scalar(), .task_ = decodeTask(entry.second)});
    }

    return out;
  }

  if (node.IsMap()) {
    for (const auto& entry : node) {
      out.push_back({.name_ = entry.first.Scalar(), .task_ = decodeTask(entry.second)});
    }

    return out;
  }

  fail(node.Mark(), "task collection must be a sequence or a mapping");
}

// ---------------------------------------------------------------------------
// Reusable components and the document
// ---------------------------------------------------------------------------

auto decodeExtension(const YAML::Node& node) -> Extension {
  requireMap(node, "extension");
  rejectUnknownKeys(node, {"extend", "when", "before", "after"}, "extension");

  Extension out{.extend_ = decodeRequired<std::string>(node, "extend", "extension"),
                .when_ = decodeOptional<std::string>(node, "when")};

  if (has(node, "before")) {
    out.before_ = decodeTasks(getChild(node, "before"));
  }

  if (has(node, "after")) {
    out.after_ = decodeTasks(getChild(node, "after"));
  }

  return out;
}

auto decodeUse(const YAML::Node& node) -> Use {
  Use out;
  requireMap(node, "use");
  rejectUnknownKeys(node,
                    {"authentications", "errors", "extensions", "functions", "retries", "secrets",
                     "timeouts", "catalogs"},
                    "use");
  if (has(node, "authentications")) {
    const YAML::Node authentications = getChild(node, "authentications");
    requireMap(authentications, "use.authentications");
    for (const auto& entry : authentications) {
      out.authentications_.emplace(entry.first.Scalar(), decodeAuthentication(entry.second));
    }
  }
  if (has(node, "errors")) {
    const YAML::Node errors = getChild(node, "errors");
    requireMap(errors, "use.errors");
    for (const auto& entry : errors) {
      out.errors_.emplace(entry.first.Scalar(), decodeError(entry.second));
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
      named_extension.extension_ = decodeExtension(entry.second);
      out.extensions_.push_back(std::move(named_extension));
    }
  }
  if (has(node, "functions")) {
    const YAML::Node functions = getChild(node, "functions");
    requireMap(functions, "use.functions");
    for (const auto& entry : functions) {
      out.functions_.emplace(entry.first.Scalar(), decodeTask(entry.second));
    }
  }
  if (has(node, "retries")) {
    const YAML::Node retries = getChild(node, "retries");
    requireMap(retries, "use.retries");
    for (const auto& entry : retries) {
      out.retries_.emplace(entry.first.Scalar(), decodeRetryPolicy(entry.second));
    }
  }
  out.secrets_ = decodeStringList(node, "secrets");
  if (has(node, "timeouts")) {
    const YAML::Node timeouts = getChild(node, "timeouts");
    requireMap(timeouts, "use.timeouts");
    for (const auto& entry : timeouts) {
      out.timeouts_.emplace(entry.first.Scalar(), decodeTimeout(entry.second));
    }
  }
  if (has(node, "catalogs")) {
    const YAML::Node catalogs = getChild(node, "catalogs");
    requireMap(catalogs, "use.catalogs");
    for (const auto& entry : catalogs) {
      out.catalogs_.emplace(entry.first.Scalar(), decodeCatalog(entry.second));
    }
  }
  return out;
}

auto decodeDocumentInfo(const YAML::Node& node) -> DocumentInfo {
  DocumentInfo out;
  requireMap(node, "document");
  rejectUnknownKeys(node,
                    {"dsl", "namespace", "name", "version", "title", "summary", "tags", "metadata"},
                    "document");
  out.dsl_ = decodeRequired<std::string>(node, "dsl", "document");
  out.namespace_ = decodeRequired<std::string>(node, "namespace", "document");
  out.name_ = decodeRequired<std::string>(node, "name", "document");
  out.version_ = decodeRequired<std::string>(node, "version", "document");
  out.title_ = decodeOptional<std::string>(node, "title");
  out.summary_ = decodeOptional<std::string>(node, "summary");
  if (has(node, "tags")) {
    out.tags_ = decodeValue(getChild(node, "tags"));
  }
  if (has(node, "metadata")) {
    out.metadata_ = decodeValue(getChild(node, "metadata"));
  }
  return out;
}

auto decodeSchedule(const YAML::Node& node) -> Schedule {
  Schedule out;
  requireMap(node, "schedule");
  rejectUnknownKeys(node, {"every", "cron", "after", "on", "read"}, "schedule");
  if (has(node, "every")) {
    out.every_ = decodeDuration(getChild(node, "every"));
  }
  out.cron_ = decodeOptional<std::string>(node, "cron");
  if (has(node, "after")) {
    out.after_ = decodeDuration(getChild(node, "after"));
  }
  if (has(node, "on")) {
    out.on_ = decodeEventConsumptionStrategy(getChild(node, "on"));
  }
  out.read_ = decodeOptional<std::string>(node, "read");
  return out;
}

auto decodeEvaluate(const YAML::Node& node) -> Evaluate {
  Evaluate out;
  requireMap(node, "evaluate");
  rejectUnknownKeys(node, {"language", "mode"}, "evaluate");
  out.language_ = decodeOptional<std::string>(node, "language");
  out.mode_ = decodeOptional<std::string>(node, "mode");
  return out;
}

auto decodeDocumentImpl(const YAML::Node& node) -> Document {
  Document out;
  requireMap(node, "workflow");
  rejectUnknownKeys(node,
                    {"document", "input", "use", "do", "timeout", "output", "schedule", "evaluate"},
                    "workflow");
  requireKey(node, "document", "workflow");
  out.document_ = decodeDocumentInfo(getChild(node, "document"));
  if (has(node, "input")) {
    out.input_ = decodeInput(getChild(node, "input"));
  }
  if (has(node, "use")) {
    out.use_ = decodeUse(getChild(node, "use"));
  }
  requireKey(node, "do", "workflow");
  out.do_ = decodeTasks(getChild(node, "do"));
  if (has(node, "timeout")) {
    const YAML::Node timeout = getChild(node, "timeout");
    if (timeout.IsScalar()) {
      out.timeout_ = timeout.as<std::string>();
    } else {
      out.timeout_ = decodeTimeout(timeout);
    }
  }
  if (has(node, "output")) {
    out.output_ = decodeOutput(getChild(node, "output"));
  }
  if (has(node, "schedule")) {
    out.schedule_ = decodeSchedule(getChild(node, "schedule"));
  }
  if (has(node, "evaluate")) {
    out.evaluate_ = decodeEvaluate(getChild(node, "evaluate"));
  }
  return out;
}

} // namespace

auto DecodeDocument(const YAML::Node& node, Document& out) -> bool {
  out = decodeDocumentImpl(node);
  return true;
}

} // namespace strij::openworkflow::parser

namespace YAML {

auto convert<strij::openworkflow::Document>::decode(const YAML::Node& node,
                                                    strij::openworkflow::Document& rhs) -> bool {
  return strij::openworkflow::parser::DecodeDocument(node, rhs);
}

} // namespace YAML
