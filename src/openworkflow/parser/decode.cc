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

#include <yaml-cpp/yaml.h>

#include "openworkflow/document.hh"
#include "openworkflow/parser/errors.hh"
#include "openworkflow/parser/one_of.hh"

namespace strij::openworkflow::parser {
namespace {

using YAML::Node;

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

[[noreturn]] void Fail(const YAML::Mark& mark, const std::string& message) {
    throw ParseError(mark, message);
}

Node Child(const Node& node, std::string_view key) {
    if (!node.IsMap()) {
        return Node();
    }
    return node[std::string(key)];
}

bool Has(const Node& node, std::string_view key) {
    const Node child = Child(node, key);
    return child.IsDefined() && !child.IsNull();
}

void RequireMap(const Node& node, std::string_view what) {
    if (!node.IsMap()) {
        Fail(node.Mark(), std::string(what) + " must be a mapping");
    }
}

void RequireSequence(const Node& node, std::string_view what) {
    if (!node.IsSequence()) {
        Fail(node.Mark(), std::string(what) + " must be a sequence");
    }
}

void RequireKey(const Node& node, std::string_view key, std::string_view what) {
    if (!Has(node, key)) {
        Fail(node.Mark(),
             "missing required key '" + std::string(key) + "' in " + std::string(what));
    }
}

void RejectUnknownKeys(const Node& node, std::initializer_list<std::string_view> allowed,
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
            Fail(entry.first.Mark(), "unknown key '" + key + "' in " + std::string(what));
        }
    }
}

template <typename T>
void DecodeRequired(const Node& node, std::string_view key, T& out, std::string_view what) {
    const Node child = Child(node, key);
    if (!child.IsDefined() || child.IsNull()) {
        Fail(node.Mark(),
             "missing required key '" + std::string(key) + "' in " + std::string(what));
    }
    out = child.as<T>();
}

template <typename T>
void DecodeOptional(const Node& node, std::string_view key, std::optional<T>& out) {
    const Node child = Child(node, key);
    if (child.IsDefined() && !child.IsNull()) {
        out = child.as<T>();
    }
}

template <typename T>
void DecodeIfPresent(const Node& node, std::string_view key, T& out) {
    const Node child = Child(node, key);
    if (child.IsDefined() && !child.IsNull()) {
        out = child.as<T>();
    }
}

void DecodeRequiredString(const Node& node, std::string_view key, std::optional<std::string>& out,
                          std::string_view what) {
    RequireKey(node, key, what);
    out = Child(node, key).as<std::string>();
}

void DecodeStringList(const Node& node, std::string_view key, std::vector<std::string>& out) {
    const Node child = Child(node, key);
    if (child.IsDefined() && !child.IsNull()) {
        RequireSequence(child, std::string(key));
        out.clear();
        for (const auto& item : child) {
            out.push_back(item.as<std::string>());
        }
    }
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void DecodeValue(const Node& node, Value& out);
void DecodeDurationUnits(const Node& node, DurationUnits& out);
void DecodeDuration(const Node& node, Duration& out);
void DecodeOAuth2Token(const Node& node, OAuth2Token& out);
void DecodeOAuth2Client(const Node& node, OAuth2AuthenticationProperties::Client& out);
void DecodeOAuth2Request(const Node& node, OAuth2AuthenticationProperties::Request& out);
void DecodeOAuth2Endpoints(const Node& node, OAuth2AuthenticationProperties::Endpoints& out);
void DecodeOAuth2Properties(const Node& node, OAuth2AuthenticationProperties& out);
void DecodeBasicAuthentication(const Node& node, BasicAuthentication& out);
void DecodeBearerAuthentication(const Node& node, BearerAuthentication& out);
void DecodeDigestAuthentication(const Node& node, DigestAuthentication& out);
void DecodeOAuth2Authentication(const Node& node, OAuth2Authentication& out);
void DecodeOidcAuthentication(const Node& node, OidcAuthentication& out);
void DecodeAuthentication(const Node& node, Authentication& out);
void DecodeEndpointObject(const Node& node, EndpointObject& out);
void DecodeEndpoint(const Node& node, Endpoint& out);
void DecodeExternalResource(const Node& node, ExternalResource& out);
void DecodeSchema(const Node& node, Schema& out);
void DecodeInput(const Node& node, Input& out);
void DecodeOutput(const Node& node, Output& out);
void DecodeExport(const Node& node, Export& out);
void DecodeError(const Node& node, Error& out);
void DecodeErrorFilter(const Node& node, ErrorFilter& out);
void DecodeBackoff(const Node& node, Backoff& out);
void DecodeJitter(const Node& node, Jitter& out);
void DecodeRetryAttempt(const Node& node, RetryLimit::Attempt& out);
void DecodeRetryLimit(const Node& node, RetryLimit& out);
void DecodeRetryPolicy(const Node& node, RetryPolicy& out);
void DecodeCatalog(const Node& node, Catalog& out);
void DecodeTimeout(const Node& node, Timeout& out);
void DecodeEventProperties(const Node& node, EventProperties& out);
void DecodeCorrelation(const Node& node, Correlation& out);
void DecodeEventFilter(const Node& node, EventFilter& out);
void DecodeEventFilterList(const Node& node, std::vector<EventFilter>& out);
void DecodeEventConsumptionStrategy(const Node& node, EventConsumptionStrategy& out);
void DecodeSubscriptionIterator(const Node& node, SubscriptionIterator& out);
void DecodeContainerLifetime(const Node& node, ContainerLifetime& out);
void DecodeContainerProcess(const Node& node, ContainerProcess& out);
void DecodeScriptProcess(const Node& node, ScriptProcess& out);
void DecodeShellProcess(const Node& node, ShellProcess& out);
void DecodeWorkflowProcess(const Node& node, WorkflowProcess& out);
void DecodeCallBody(const Node& node, CallBody& out);
void DecodeDoBody(const Node& node, DoBody& out);
void DecodeEmitBody(const Node& node, EmitBody& out);
void DecodeEvent(const Node& node, Event& out);
void DecodeForSpec(const Node& node, ForSpec& out);
void DecodeForBody(const Node& node, ForBody& out);
void DecodeForkBody(const Node& node, ForkBody& out);
void DecodeListenBody(const Node& node, ListenBody& out);
void DecodeRaiseBody(const Node& node, RaiseBody& out);
void DecodeRunBody(const Node& node, RunBody& out);
void DecodeSetBody(const Node& node, SetBody& out);
void DecodeSwitchBody(const Node& node, SwitchBody& out);
void DecodeCatch(const Node& node, Catch& out);
void DecodeTryBody(const Node& node, TryBody& out);
void DecodeWaitBody(const Node& node, WaitBody& out);
void DecodeTask(const Node& node, Task& out);
void DecodeTasks(const Node& node, Tasks& out);
void DecodeExtension(const Node& node, Extension& out);
void DecodeUse(const Node& node, Use& out);
void DecodeDocumentInfo(const Node& node, DocumentInfo& out);
void DecodeSchedule(const Node& node, Schedule& out);
void DecodeEvaluate(const Node& node, Evaluate& out);

// ---------------------------------------------------------------------------
// Leaf and shared shapes
// ---------------------------------------------------------------------------

void DecodeValue(const Node& node, Value& out) {
    if (!node.IsDefined() || node.IsNull()) {
        out = Value();
        return;
    }
    if (node.IsSequence()) {
        Value::Array array;
        array.reserve(node.size());
        for (const auto& item : node) {
            Value element;
            DecodeValue(item, element);
            array.push_back(std::move(element));
        }
        out = std::move(array);
        return;
    }
    if (node.IsMap()) {
        Value::Object object;
        for (const auto& entry : node) {
            Value element;
            DecodeValue(entry.second, element);
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

void DecodeDurationUnits(const Node& node, DurationUnits& out) {
    RequireMap(node, "duration");
    RejectUnknownKeys(node, {"days", "hours", "minutes", "seconds", "milliseconds"}, "duration");
    DecodeOptional(node, "days", out.days_);
    DecodeOptional(node, "hours", out.hours_);
    DecodeOptional(node, "minutes", out.minutes_);
    DecodeOptional(node, "seconds", out.seconds_);
    DecodeOptional(node, "milliseconds", out.milliseconds_);
}

void DecodeDuration(const Node& node, Duration& out) {
    if (node.IsScalar()) {
        out = node.as<std::string>();
        return;
    }
    if (node.IsMap()) {
        DurationUnits units;
        DecodeDurationUnits(node, units);
        out = std::move(units);
        return;
    }
    Fail(node.Mark(), "duration must be a string or a mapping");
}

void DecodeOAuth2Token(const Node& node, OAuth2Token& out) {
    RequireMap(node, "oauth2 token");
    RejectUnknownKeys(node, {"token", "type"}, "oauth2 token");
    DecodeRequired(node, "token", out.token_, "oauth2 token");
    DecodeRequired(node, "type", out.type_, "oauth2 token");
}

void DecodeOAuth2Client(const Node& node, OAuth2AuthenticationProperties::Client& out) {
    RequireMap(node, "oauth2 client");
    RejectUnknownKeys(node, {"id", "secret", "assertion", "authentication"}, "oauth2 client");
    DecodeOptional(node, "id", out.id_);
    DecodeOptional(node, "secret", out.secret_);
    DecodeOptional(node, "assertion", out.assertion_);
    DecodeOptional(node, "authentication", out.authentication_);
}

void DecodeOAuth2Request(const Node& node, OAuth2AuthenticationProperties::Request& out) {
    RequireMap(node, "oauth2 request");
    RejectUnknownKeys(node, {"encoding"}, "oauth2 request");
    DecodeOptional(node, "encoding", out.encoding_);
}

void DecodeOAuth2Endpoints(const Node& node, OAuth2AuthenticationProperties::Endpoints& out) {
    RequireMap(node, "oauth2 endpoints");
    RejectUnknownKeys(node, {"token", "introspection"}, "oauth2 endpoints");
    DecodeOptional(node, "token", out.token_);
    DecodeOptional(node, "introspection", out.introspection_);
}

void DecodeOAuth2Properties(const Node& node, OAuth2AuthenticationProperties& out) {
    RequireMap(node, "oauth2 authentication");
    RejectUnknownKeys(node,
                      {"authority", "grant", "client", "request", "endpoints", "issuers", "scopes",
                       "audiences", "username", "password", "subject", "actor"},
                      "oauth2 authentication");
    DecodeRequired(node, "authority", out.authority_, "oauth2 authentication");
    DecodeRequired(node, "grant", out.grant_, "oauth2 authentication");
    if (Has(node, "client")) {
        out.client_.emplace();
        DecodeOAuth2Client(Child(node, "client"), *out.client_);
    }
    if (Has(node, "request")) {
        out.request_.emplace();
        DecodeOAuth2Request(Child(node, "request"), *out.request_);
    }
    if (Has(node, "endpoints")) {
        out.endpoints_.emplace();
        DecodeOAuth2Endpoints(Child(node, "endpoints"), *out.endpoints_);
    }
    DecodeStringList(node, "issuers", out.issuers_);
    DecodeStringList(node, "scopes", out.scopes_);
    DecodeStringList(node, "audiences", out.audiences_);
    DecodeOptional(node, "username", out.username_);
    DecodeOptional(node, "password", out.password_);
    if (Has(node, "subject")) {
        out.subject_.emplace();
        DecodeOAuth2Token(Child(node, "subject"), *out.subject_);
    }
    if (Has(node, "actor")) {
        out.actor_.emplace();
        DecodeOAuth2Token(Child(node, "actor"), *out.actor_);
    }
}

void DecodeBasicAuthentication(const Node& node, BasicAuthentication& out) {
    RequireMap(node, "basic authentication");
    if (Has(node, "use")) {
        RejectUnknownKeys(node, {"use"}, "basic authentication reference");
        DecodeRequiredString(node, "use", out.use_, "basic authentication reference");
        return;
    }
    RejectUnknownKeys(node, {"username", "password"}, "basic authentication");
    DecodeRequiredString(node, "username", out.username_, "basic authentication");
    DecodeRequiredString(node, "password", out.password_, "basic authentication");
}

void DecodeBearerAuthentication(const Node& node, BearerAuthentication& out) {
    RequireMap(node, "bearer authentication");
    if (Has(node, "use")) {
        RejectUnknownKeys(node, {"use"}, "bearer authentication reference");
        DecodeRequiredString(node, "use", out.use_, "bearer authentication reference");
        return;
    }
    RejectUnknownKeys(node, {"token"}, "bearer authentication");
    DecodeRequiredString(node, "token", out.token_, "bearer authentication");
}

void DecodeDigestAuthentication(const Node& node, DigestAuthentication& out) {
    RequireMap(node, "digest authentication");
    if (Has(node, "use")) {
        RejectUnknownKeys(node, {"use"}, "digest authentication reference");
        DecodeRequiredString(node, "use", out.use_, "digest authentication reference");
        return;
    }
    RejectUnknownKeys(node, {"username", "password"}, "digest authentication");
    DecodeRequiredString(node, "username", out.username_, "digest authentication");
    DecodeRequiredString(node, "password", out.password_, "digest authentication");
}

void DecodeOAuth2Authentication(const Node& node, OAuth2Authentication& out) {
    RequireMap(node, "oauth2 authentication");
    if (Has(node, "use")) {
        RejectUnknownKeys(node, {"use"}, "oauth2 authentication reference");
        DecodeRequiredString(node, "use", out.use_, "oauth2 authentication reference");
        return;
    }
    out.properties_.emplace();
    DecodeOAuth2Properties(node, *out.properties_);
}

void DecodeOidcAuthentication(const Node& node, OidcAuthentication& out) {
    RequireMap(node, "oidc authentication");
    if (Has(node, "use")) {
        RejectUnknownKeys(node, {"use"}, "oidc authentication reference");
        DecodeRequiredString(node, "use", out.use_, "oidc authentication reference");
        return;
    }
    out.properties_.emplace();
    DecodeOAuth2Properties(node, *out.properties_);
}

void DecodeAuthentication(const Node& node, Authentication& out) {
    RequireMap(node, "authentication");
    if (Has(node, "use")) {
        RejectUnknownKeys(node, {"use"}, "authentication");
        AuthenticationReference reference;
        DecodeRequired(node, "use", reference.use_, "authentication");
        out.scheme_ = std::move(reference);
        return;
    }
    const std::string_view scheme =
        SelectOne(node, {"basic", "bearer", "digest", "oauth2", "oidc"}, "authentication scheme");
    RejectUnknownKeys(node, {"basic", "bearer", "digest", "oauth2", "oidc"}, "authentication");
    const Node scheme_node = Child(node, scheme);
    if (scheme == "basic") {
        BasicAuthentication value;
        DecodeBasicAuthentication(scheme_node, value);
        out.scheme_ = std::move(value);
    } else if (scheme == "bearer") {
        BearerAuthentication value;
        DecodeBearerAuthentication(scheme_node, value);
        out.scheme_ = std::move(value);
    } else if (scheme == "digest") {
        DigestAuthentication value;
        DecodeDigestAuthentication(scheme_node, value);
        out.scheme_ = std::move(value);
    } else if (scheme == "oauth2") {
        OAuth2Authentication value;
        DecodeOAuth2Authentication(scheme_node, value);
        out.scheme_ = std::move(value);
    } else {
        OidcAuthentication value;
        DecodeOidcAuthentication(scheme_node, value);
        out.scheme_ = std::move(value);
    }
}

void DecodeEndpointObject(const Node& node, EndpointObject& out) {
    RequireMap(node, "endpoint");
    RejectUnknownKeys(node, {"uri", "authentication"}, "endpoint");
    DecodeRequired(node, "uri", out.uri_, "endpoint");
    if (Has(node, "authentication")) {
        out.authentication_.emplace();
        DecodeAuthentication(Child(node, "authentication"), *out.authentication_);
    }
}

void DecodeEndpoint(const Node& node, Endpoint& out) {
    if (node.IsScalar()) {
        out.value_ = node.as<std::string>();
        return;
    }
    if (node.IsMap()) {
        EndpointObject value;
        DecodeEndpointObject(node, value);
        out.value_ = std::move(value);
        return;
    }
    Fail(node.Mark(), "endpoint must be a string or a mapping");
}

void DecodeExternalResource(const Node& node, ExternalResource& out) {
    RequireMap(node, "external resource");
    RejectUnknownKeys(node, {"name", "endpoint"}, "external resource");
    DecodeOptional(node, "name", out.name_);
    RequireKey(node, "endpoint", "external resource");
    DecodeEndpoint(Child(node, "endpoint"), out.endpoint_);
}

void DecodeSchema(const Node& node, Schema& out) {
    RequireMap(node, "schema");
    RejectUnknownKeys(node, {"format", "document", "resource"}, "schema");
    DecodeIfPresent(node, "format", out.format_);
    const std::string_view source = SelectOne(node, {"document", "resource"}, "schema source");
    const Node source_node = Child(node, source);
    if (source == "document") {
        Value value;
        DecodeValue(source_node, value);
        out.source_ = std::move(value);
    } else {
        ExternalResource value;
        DecodeExternalResource(source_node, value);
        out.source_ = std::move(value);
    }
}

void DecodeInput(const Node& node, Input& out) {
    RequireMap(node, "input");
    RejectUnknownKeys(node, {"schema", "from"}, "input");
    if (Has(node, "schema")) {
        out.schema_.emplace();
        DecodeSchema(Child(node, "schema"), *out.schema_);
    }
    if (Has(node, "from")) {
        const Node child = Child(node, "from");
        if (child.IsScalar()) {
            out.from_ = child.as<std::string>();
        } else {
            Value value;
            DecodeValue(child, value);
            out.from_ = std::move(value);
        }
    }
}

void DecodeOutput(const Node& node, Output& out) {
    RequireMap(node, "output");
    RejectUnknownKeys(node, {"schema", "as"}, "output");
    if (Has(node, "schema")) {
        out.schema_.emplace();
        DecodeSchema(Child(node, "schema"), *out.schema_);
    }
    if (Has(node, "as")) {
        const Node child = Child(node, "as");
        if (child.IsScalar()) {
            out.as_ = child.as<std::string>();
        } else {
            Value value;
            DecodeValue(child, value);
            out.as_ = std::move(value);
        }
    }
}

void DecodeExport(const Node& node, Export& out) {
    RequireMap(node, "export");
    RejectUnknownKeys(node, {"schema", "as"}, "export");
    if (Has(node, "schema")) {
        out.schema_.emplace();
        DecodeSchema(Child(node, "schema"), *out.schema_);
    }
    if (Has(node, "as")) {
        const Node child = Child(node, "as");
        if (child.IsScalar()) {
            out.as_ = child.as<std::string>();
        } else {
            Value value;
            DecodeValue(child, value);
            out.as_ = std::move(value);
        }
    }
}

void DecodeError(const Node& node, Error& out) {
    RequireMap(node, "error");
    RejectUnknownKeys(node, {"type", "status", "instance", "title", "detail"}, "error");
    DecodeRequired(node, "type", out.type_, "error");
    DecodeRequired(node, "status", out.status_, "error");
    DecodeOptional(node, "instance", out.instance_);
    DecodeOptional(node, "title", out.title_);
    DecodeOptional(node, "detail", out.detail_);
}

void DecodeErrorFilter(const Node& node, ErrorFilter& out) {
    RequireMap(node, "error filter");
    RejectUnknownKeys(node, {"type", "status", "instance", "title", "detail"}, "error filter");
    DecodeOptional(node, "type", out.type_);
    DecodeOptional(node, "status", out.status_);
    DecodeOptional(node, "instance", out.instance_);
    DecodeOptional(node, "title", out.title_);
    DecodeOptional(node, "detail", out.detail_);
}

void DecodeBackoff(const Node& node, Backoff& out) {
    RequireMap(node, "retry backoff");
    const std::string_view kind =
        SelectOne(node, {"constant", "exponential", "linear"}, "retry backoff branch");
    RejectUnknownKeys(node, {"constant", "exponential", "linear"}, "retry backoff");
    out.kind_ = std::string(kind);
    DecodeValue(Child(node, kind), out.parameters_);
}

void DecodeJitter(const Node& node, Jitter& out) {
    RequireMap(node, "retry jitter");
    RejectUnknownKeys(node, {"from", "to"}, "retry jitter");
    RequireKey(node, "from", "retry jitter");
    RequireKey(node, "to", "retry jitter");
    DecodeDuration(Child(node, "from"), out.from_);
    DecodeDuration(Child(node, "to"), out.to_);
}

void DecodeRetryAttempt(const Node& node, RetryLimit::Attempt& out) {
    RequireMap(node, "retry attempt");
    RejectUnknownKeys(node, {"count", "duration"}, "retry attempt");
    DecodeOptional(node, "count", out.count_);
    if (Has(node, "duration")) {
        DecodeDuration(Child(node, "duration"), out.duration_.emplace());
    }
}

void DecodeRetryLimit(const Node& node, RetryLimit& out) {
    RequireMap(node, "retry limit");
    RejectUnknownKeys(node, {"attempt", "duration"}, "retry limit");
    if (Has(node, "attempt")) {
        out.attempt_.emplace();
        DecodeRetryAttempt(Child(node, "attempt"), *out.attempt_);
    }
    if (Has(node, "duration")) {
        DecodeDuration(Child(node, "duration"), out.duration_.emplace());
    }
}

void DecodeRetryPolicy(const Node& node, RetryPolicy& out) {
    RequireMap(node, "retry policy");
    RejectUnknownKeys(node, {"when", "exceptWhen", "delay", "backoff", "limit", "jitter"},
                      "retry policy");
    DecodeOptional(node, "when", out.when_);
    DecodeOptional(node, "exceptWhen", out.exceptWhen_);
    if (Has(node, "delay")) {
        DecodeDuration(Child(node, "delay"), out.delay_.emplace());
    }
    if (Has(node, "backoff")) {
        out.backoff_.emplace();
        DecodeBackoff(Child(node, "backoff"), *out.backoff_);
    }
    if (Has(node, "limit")) {
        out.limit_.emplace();
        DecodeRetryLimit(Child(node, "limit"), *out.limit_);
    }
    if (Has(node, "jitter")) {
        out.jitter_.emplace();
        DecodeJitter(Child(node, "jitter"), *out.jitter_);
    }
}

void DecodeCatalog(const Node& node, Catalog& out) {
    RequireMap(node, "catalog");
    RejectUnknownKeys(node, {"endpoint"}, "catalog");
    RequireKey(node, "endpoint", "catalog");
    DecodeEndpoint(Child(node, "endpoint"), out.endpoint_);
}

void DecodeTimeout(const Node& node, Timeout& out) {
    RequireMap(node, "timeout");
    RejectUnknownKeys(node, {"after"}, "timeout");
    RequireKey(node, "after", "timeout");
    DecodeDuration(Child(node, "after"), out.after_);
}

// ---------------------------------------------------------------------------
// Event machinery
// ---------------------------------------------------------------------------

void DecodeEventProperties(const Node& node, EventProperties& out) {
    RequireMap(node, "event properties");
    RejectUnknownKeys(node,
                      {"id", "source", "type", "time", "subject", "datacontenttype", "dataschema",
                       "data"},
                      "event properties");
    DecodeOptional(node, "id", out.id_);
    DecodeOptional(node, "source", out.source_);
    DecodeOptional(node, "type", out.type_);
    DecodeOptional(node, "time", out.time_);
    DecodeOptional(node, "subject", out.subject_);
    DecodeOptional(node, "datacontenttype", out.datacontenttype_);
    DecodeOptional(node, "dataschema", out.dataschema_);
    if (Has(node, "data")) {
        out.data_.emplace();
        DecodeValue(Child(node, "data"), *out.data_);
    }
}

void DecodeCorrelation(const Node& node, Correlation& out) {
    RequireMap(node, "correlation");
    RejectUnknownKeys(node, {"from", "expect"}, "correlation");
    DecodeRequired(node, "from", out.from_, "correlation");
    DecodeOptional(node, "expect", out.expect_);
}

void DecodeEventFilter(const Node& node, EventFilter& out) {
    RequireMap(node, "event filter");
    RejectUnknownKeys(node, {"with", "correlate"}, "event filter");
    RequireKey(node, "with", "event filter");
    DecodeEventProperties(Child(node, "with"), out.with_);
    if (Has(node, "correlate")) {
        const Node correlate = Child(node, "correlate");
        RequireMap(correlate, "event correlation");
        for (const auto& entry : correlate) {
            Correlation value;
            DecodeCorrelation(entry.second, value);
            out.correlate_.emplace(entry.first.Scalar(), std::move(value));
        }
    }
}

void DecodeEventFilterList(const Node& node, std::vector<EventFilter>& out) {
    RequireSequence(node, "event list");
    out.clear();
    for (const auto& item : node) {
        EventFilter value;
        DecodeEventFilter(item, value);
        out.push_back(std::move(value));
    }
}

void DecodeEventConsumptionStrategy(const Node& node, EventConsumptionStrategy& out) {
    RequireMap(node, "event consumption strategy");
    const std::string_view mode =
        SelectOne(node, {"all", "any", "one"}, "event consumption strategy");
    RejectUnknownKeys(node, {"all", "any", "one", "until"}, "event consumption strategy");
    if (mode == "all") {
        out.mode_ = EventConsumptionStrategy::Mode::kAll;
        DecodeEventFilterList(Child(node, "all"), out.filters_);
    } else if (mode == "any") {
        out.mode_ = EventConsumptionStrategy::Mode::kAny;
        DecodeEventFilterList(Child(node, "any"), out.filters_);
        if (Has(node, "until")) {
            out.until_.emplace();
            DecodeValue(Child(node, "until"), *out.until_);
        }
    } else {
        out.mode_ = EventConsumptionStrategy::Mode::kOne;
        EventFilter value;
        DecodeEventFilter(Child(node, "one"), value);
        out.filters_.push_back(std::move(value));
    }
}

void DecodeSubscriptionIterator(const Node& node, SubscriptionIterator& out) {
    RequireMap(node, "subscription iterator");
    RejectUnknownKeys(node, {"item", "at", "do", "output", "export"}, "subscription iterator");
    DecodeOptional(node, "item", out.item_);
    DecodeOptional(node, "at", out.at_);
    if (Has(node, "do")) {
        DecodeTasks(Child(node, "do"), out.do_);
    }
    if (Has(node, "output")) {
        out.output_.emplace();
        DecodeOutput(Child(node, "output"), *out.output_);
    }
    if (Has(node, "export")) {
        out.export_.emplace();
        DecodeExport(Child(node, "export"), *out.export_);
    }
}

// ---------------------------------------------------------------------------
// Task bodies
// ---------------------------------------------------------------------------

void DecodeContainerLifetime(const Node& node, ContainerLifetime& out) {
    RequireMap(node, "container lifetime");
    RejectUnknownKeys(node, {"cleanup", "after"}, "container lifetime");
    DecodeRequired(node, "cleanup", out.cleanup_, "container lifetime");
    if (Has(node, "after")) {
        DecodeDuration(Child(node, "after"), out.after_.emplace());
    }
}

void DecodeContainerProcess(const Node& node, ContainerProcess& out) {
    RequireMap(node, "container process");
    RejectUnknownKeys(node,
                      {"image", "name", "command", "ports", "volumes", "environment", "stdin",
                       "arguments", "lifetime", "pullPolicy"},
                      "container process");
    DecodeRequired(node, "image", out.image_, "container process");
    DecodeOptional(node, "name", out.name_);
    DecodeOptional(node, "command", out.command_);
    if (Has(node, "ports")) {
        out.ports_.emplace();
        DecodeValue(Child(node, "ports"), *out.ports_);
    }
    if (Has(node, "volumes")) {
        out.volumes_.emplace();
        DecodeValue(Child(node, "volumes"), *out.volumes_);
    }
    if (Has(node, "environment")) {
        out.environment_.emplace();
        DecodeValue(Child(node, "environment"), *out.environment_);
    }
    DecodeOptional(node, "stdin", out.stdin_);
    DecodeStringList(node, "arguments", out.arguments_);
    if (Has(node, "lifetime")) {
        out.lifetime_.emplace();
        DecodeContainerLifetime(Child(node, "lifetime"), *out.lifetime_);
    }
    DecodeOptional(node, "pullPolicy", out.pull_policy_);
}

void DecodeScriptProcess(const Node& node, ScriptProcess& out) {
    RequireMap(node, "script process");
    const std::string_view source =
        SelectOne(node, {"code", "resource"}, "script process source");
    RejectUnknownKeys(node, {"language", "stdin", "arguments", "environment", "code", "resource"},
                      "script process");
    DecodeRequired(node, "language", out.language_, "script process");
    DecodeOptional(node, "stdin", out.stdin_);
    DecodeStringList(node, "arguments", out.arguments_);
    if (Has(node, "environment")) {
        out.environment_.emplace();
        DecodeValue(Child(node, "environment"), *out.environment_);
    }
    if (source == "code") {
        ScriptSourceCode code;
        DecodeRequired(node, "code", code.code_, "script process");
        out.source_ = std::move(code);
    } else {
        ScriptSourceResource resource;
        DecodeExternalResource(Child(node, "resource"), resource.source_);
        out.source_ = std::move(resource);
    }
}

void DecodeShellProcess(const Node& node, ShellProcess& out) {
    RequireMap(node, "shell process");
    RejectUnknownKeys(node, {"command", "stdin", "arguments", "environment"}, "shell process");
    DecodeRequired(node, "command", out.command_, "shell process");
    DecodeOptional(node, "stdin", out.stdin_);
    DecodeStringList(node, "arguments", out.arguments_);
    if (Has(node, "environment")) {
        out.environment_.emplace();
        DecodeValue(Child(node, "environment"), *out.environment_);
    }
}

void DecodeWorkflowProcess(const Node& node, WorkflowProcess& out) {
    RequireMap(node, "workflow process");
    RejectUnknownKeys(node, {"namespace", "name", "version", "input"}, "workflow process");
    DecodeRequired(node, "namespace", out.namespace_, "workflow process");
    DecodeRequired(node, "name", out.name_, "workflow process");
    DecodeRequired(node, "version", out.version_, "workflow process");
    if (Has(node, "input")) {
        out.input_.emplace();
        DecodeValue(Child(node, "input"), *out.input_);
    }
}

void DecodeCallBody(const Node& node, CallBody& out) {
    RequireKey(node, "call", "call task");
    DecodeRequired(node, "call", out.call_, "call task");
    if (Has(node, "with")) {
        DecodeValue(Child(node, "with"), out.with_);
    }
}

void DecodeDoBody(const Node& node, DoBody& out) {
    RequireKey(node, "do", "do task");
    DecodeTasks(Child(node, "do"), out.do_);
}

void DecodeEvent(const Node& node, Event& out) {
    RequireMap(node, "event");
    RejectUnknownKeys(node, {"with"}, "event");
    if (Has(node, "with")) {
        DecodeEventProperties(Child(node, "with"), out.with_);
    }
}

void DecodeEmitBody(const Node& node, EmitBody& out) {
    RequireKey(node, "emit", "emit task");
    const Node emit = Child(node, "emit");
    RequireMap(emit, "emit task");
    RejectUnknownKeys(emit, {"event"}, "emit task");
    RequireKey(emit, "event", "emit task");
    DecodeEvent(Child(emit, "event"), out.event_);
}

void DecodeForSpec(const Node& node, ForSpec& out) {
    RequireMap(node, "for");
    RejectUnknownKeys(node, {"each", "at", "in"}, "for");
    DecodeOptional(node, "each", out.each_);
    DecodeOptional(node, "at", out.at_);
    RequireKey(node, "in", "for");
    const Node in = Child(node, "in");
    if (in.IsScalar()) {
        out.in_ = in.as<std::string>();
    } else {
        Value value;
        DecodeValue(in, value);
        out.in_ = std::move(value);
    }
}

void DecodeForBody(const Node& node, ForBody& out) {
    RequireKey(node, "for", "for task");
    DecodeForSpec(Child(node, "for"), out.for_);
    DecodeOptional(node, "while", out.while_);
    RequireKey(node, "do", "for task");
    DecodeTasks(Child(node, "do"), out.do_);
}

void DecodeForkBody(const Node& node, ForkBody& out) {
    RequireKey(node, "fork", "fork task");
    const Node fork = Child(node, "fork");
    RequireMap(fork, "fork task");
    RejectUnknownKeys(fork, {"branches", "compete"}, "fork task");
    RequireKey(fork, "branches", "fork task");
    DecodeTasks(Child(fork, "branches"), out.branches_);
    DecodeIfPresent(fork, "compete", out.compete_);
}

void DecodeListenBody(const Node& node, ListenBody& out) {
    RequireKey(node, "listen", "listen task");
    const Node listen = Child(node, "listen");
    RequireMap(listen, "listen task");
    RejectUnknownKeys(listen, {"to", "read"}, "listen task");
    RequireKey(listen, "to", "listen task");
    DecodeEventConsumptionStrategy(Child(listen, "to"), out.to_);
    DecodeOptional(listen, "read", out.read_);
    if (Has(node, "foreach")) {
        out.foreach_.emplace();
        DecodeSubscriptionIterator(Child(node, "foreach"), *out.foreach_);
    }
}

void DecodeRaiseBody(const Node& node, RaiseBody& out) {
    RequireKey(node, "raise", "raise task");
    const Node raise = Child(node, "raise");
    RequireMap(raise, "raise task");
    RejectUnknownKeys(raise, {"error"}, "raise task");
    RequireKey(raise, "error", "raise task");
    const Node error = Child(raise, "error");
    if (error.IsScalar()) {
        out.error_ = error.as<std::string>();
    } else {
        Error value;
        DecodeError(error, value);
        out.error_ = std::move(value);
    }
}

void DecodeRunBody(const Node& node, RunBody& out) {
    RequireKey(node, "run", "run task");
    const Node run = Child(node, "run");
    RequireMap(run, "run task");
    const std::string_view kind =
        SelectOne(run, {"container", "shell", "script", "workflow"}, "run process kind");
    RejectUnknownKeys(run, {"container", "shell", "script", "workflow", "await", "return"},
                      "run task");
    const Node process = Child(run, kind);
    if (kind == "container") {
        ContainerProcess value;
        DecodeContainerProcess(process, value);
        out.process_ = std::move(value);
    } else if (kind == "shell") {
        ShellProcess value;
        DecodeShellProcess(process, value);
        out.process_ = std::move(value);
    } else if (kind == "script") {
        ScriptProcess value;
        DecodeScriptProcess(process, value);
        out.process_ = std::move(value);
    } else {
        WorkflowProcess value;
        DecodeWorkflowProcess(process, value);
        out.process_ = std::move(value);
    }
    DecodeOptional(run, "await", out.await_);
    DecodeOptional(run, "return", out.return_);
}

void DecodeSetBody(const Node& node, SetBody& out) {
    RequireKey(node, "set", "set task");
    const Node set = Child(node, "set");
    if (set.IsScalar()) {
        out.set_ = set.as<std::string>();
    } else {
        Value value;
        DecodeValue(set, value);
        out.set_ = std::move(value);
    }
}

void DecodeSwitchBody(const Node& node, SwitchBody& out) {
    RequireKey(node, "switch", "switch task");
    const Node node_switch = Child(node, "switch");
    RequireSequence(node_switch, "switch task");
    out.cases_.clear();
    for (const auto& item : node_switch) {
        RequireMap(item, "switch case");
        if (item.size() != 1) {
            Fail(item.Mark(), "each switch case must have exactly one name");
        }
        const auto entry = *item.begin();
        NamedCase named_case;
        named_case.name_ = entry.first.Scalar();
        const Node body = entry.second;
        RequireMap(body, "switch case");
        RejectUnknownKeys(body, {"when", "then"}, "switch case");
        DecodeOptional(body, "when", named_case.when_);
        DecodeOptional(body, "then", named_case.then_);
        out.cases_.push_back(std::move(named_case));
    }
}

void DecodeCatch(const Node& node, Catch& out) {
    RequireMap(node, "catch");
    RejectUnknownKeys(node, {"errors", "as", "when", "exceptWhen", "retry", "do", "then"},
                      "catch");
    if (Has(node, "errors")) {
        const Node errors = Child(node, "errors");
        RequireMap(errors, "catch errors");
        RejectUnknownKeys(errors, {"with"}, "catch errors");
        if (Has(errors, "with")) {
            out.errors_.emplace();
            DecodeErrorFilter(Child(errors, "with"), *out.errors_);
        }
    }
    DecodeOptional(node, "as", out.as_);
    DecodeOptional(node, "when", out.when_);
    DecodeOptional(node, "exceptWhen", out.exceptWhen_);
    if (Has(node, "retry")) {
        const Node retry = Child(node, "retry");
        if (retry.IsScalar()) {
            out.retry_ = retry.as<std::string>();
        } else {
            RetryPolicy value;
            DecodeRetryPolicy(retry, value);
            out.retry_ = std::move(value);
        }
    }
    if (Has(node, "do")) {
        DecodeTasks(Child(node, "do"), out.do_);
    }
    DecodeOptional(node, "then", out.then_);
}

void DecodeTryBody(const Node& node, TryBody& out) {
    RequireKey(node, "try", "try task");
    DecodeTasks(Child(node, "try"), out.try_);
    if (Has(node, "catch")) {
        out.catch_.emplace();
        DecodeCatch(Child(node, "catch"), *out.catch_);
    }
}

void DecodeWaitBody(const Node& node, WaitBody& out) {
    RequireKey(node, "wait", "wait task");
    DecodeDuration(Child(node, "wait"), out.wait_);
}

// ---------------------------------------------------------------------------
// Task union and collections
// ---------------------------------------------------------------------------

void DecodeTask(const Node& node, Task& out) {
    RequireMap(node, "task");
    RejectUnknownKeys(node,
                      {"if", "input", "output", "export", "timeout", "then", "metadata", "call",
                       "do", "emit", "for", "fork", "listen", "raise", "run", "set", "switch",
                       "try", "wait", "with", "foreach", "while", "catch"},
                      "task");
    DecodeOptional(node, "if", out.if_);
    if (Has(node, "input")) {
        out.input_.emplace();
        DecodeInput(Child(node, "input"), *out.input_);
    }
    if (Has(node, "output")) {
        out.output_.emplace();
        DecodeOutput(Child(node, "output"), *out.output_);
    }
    if (Has(node, "export")) {
        out.export_.emplace();
        DecodeExport(Child(node, "export"), *out.export_);
    }
    if (Has(node, "timeout")) {
        const Node timeout = Child(node, "timeout");
        if (timeout.IsScalar()) {
            out.timeout_ = timeout.as<std::string>();
        } else {
            Timeout value;
            DecodeTimeout(timeout, value);
            out.timeout_ = std::move(value);
        }
    }
    DecodeOptional(node, "then", out.then_);
    if (Has(node, "metadata")) {
        DecodeValue(Child(node, "metadata"), out.metadata_);
    }

    // The `do` key is overloaded: it is the do task kind, but also a sibling
    // collection of the for task. When `for` is present, `do` belongs to it.
    std::vector<std::string_view> present;
    for (const std::string_view kind : {"call", "do", "emit", "for", "fork", "listen", "raise",
                                        "run", "set", "switch", "try", "wait"}) {
        if (Has(node, kind)) {
            present.push_back(kind);
        }
    }
    if (Has(node, "for")) {
        present.erase(std::remove(present.begin(), present.end(), "do"), present.end());
    }

    if (present.empty()) {
        Fail(node.Mark(),
             "task must declare exactly one task kind (call, do, emit, for, fork, listen, raise, "
             "run, set, switch, try, wait)");
    }
    if (present.size() > 1) {
        Fail(node.Mark(), "task declares mutually exclusive kinds '" + std::string(present[0]) +
                              "' and '" + std::string(present[1]) + "'");
    }

    const std::string_view kind = present.front();
    if (kind == "call") {
        CallBody body;
        DecodeCallBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "do") {
        DoBody body;
        DecodeDoBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "emit") {
        EmitBody body;
        DecodeEmitBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "for") {
        ForBody body;
        DecodeForBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "fork") {
        ForkBody body;
        DecodeForkBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "listen") {
        ListenBody body;
        DecodeListenBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "raise") {
        RaiseBody body;
        DecodeRaiseBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "run") {
        RunBody body;
        DecodeRunBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "set") {
        SetBody body;
        DecodeSetBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "switch") {
        SwitchBody body;
        DecodeSwitchBody(node, body);
        out.body_ = std::move(body);
    } else if (kind == "try") {
        TryBody body;
        DecodeTryBody(node, body);
        out.body_ = std::move(body);
    } else {
        WaitBody body;
        DecodeWaitBody(node, body);
        out.body_ = std::move(body);
    }
}

void DecodeTasks(const Node& node, Tasks& out) {
    out.clear();
    if (node.IsSequence()) {
        for (const auto& item : node) {
            RequireMap(item, "task collection");
            if (item.size() != 1) {
                Fail(item.Mark(), "each task entry must be a single-key mapping");
            }
            const auto entry = *item.begin();
            NamedTask named_task;
            named_task.name_ = entry.first.Scalar();
            DecodeTask(entry.second, named_task.task_);
            out.push_back(std::move(named_task));
        }
        return;
    }
    if (node.IsMap()) {
        for (const auto& entry : node) {
            NamedTask named_task;
            named_task.name_ = entry.first.Scalar();
            DecodeTask(entry.second, named_task.task_);
            out.push_back(std::move(named_task));
        }
        return;
    }
    Fail(node.Mark(), "task collection must be a sequence or a mapping");
}

// ---------------------------------------------------------------------------
// Reusable components and the document
// ---------------------------------------------------------------------------

void DecodeExtension(const Node& node, Extension& out) {
    RequireMap(node, "extension");
    RejectUnknownKeys(node, {"extend", "when", "before", "after"}, "extension");
    DecodeRequired(node, "extend", out.extend_, "extension");
    DecodeOptional(node, "when", out.when_);
    if (Has(node, "before")) {
        DecodeTasks(Child(node, "before"), out.before_);
    }
    if (Has(node, "after")) {
        DecodeTasks(Child(node, "after"), out.after_);
    }
}

void DecodeUse(const Node& node, Use& out) {
    RequireMap(node, "use");
    RejectUnknownKeys(node,
                      {"authentications", "errors", "extensions", "functions", "retries", "secrets",
                       "timeouts", "catalogs"},
                      "use");
    if (Has(node, "authentications")) {
        const Node authentications = Child(node, "authentications");
        RequireMap(authentications, "use.authentications");
        for (const auto& entry : authentications) {
            Authentication value;
            DecodeAuthentication(entry.second, value);
            out.authentications_.emplace(entry.first.Scalar(), std::move(value));
        }
    }
    if (Has(node, "errors")) {
        const Node errors = Child(node, "errors");
        RequireMap(errors, "use.errors");
        for (const auto& entry : errors) {
            Error value;
            DecodeError(entry.second, value);
            out.errors_.emplace(entry.first.Scalar(), std::move(value));
        }
    }
    if (Has(node, "extensions")) {
        const Node extensions = Child(node, "extensions");
        RequireSequence(extensions, "use.extensions");
        for (const auto& item : extensions) {
            RequireMap(item, "use.extensions");
            if (item.size() != 1) {
                Fail(item.Mark(), "each extension must be a single-key mapping");
            }
            const auto entry = *item.begin();
            NamedExtension named_extension;
            named_extension.name_ = entry.first.Scalar();
            DecodeExtension(entry.second, named_extension.extension_);
            out.extensions_.push_back(std::move(named_extension));
        }
    }
    if (Has(node, "functions")) {
        const Node functions = Child(node, "functions");
        RequireMap(functions, "use.functions");
        for (const auto& entry : functions) {
            Task value;
            DecodeTask(entry.second, value);
            out.functions_.emplace(entry.first.Scalar(), std::move(value));
        }
    }
    if (Has(node, "retries")) {
        const Node retries = Child(node, "retries");
        RequireMap(retries, "use.retries");
        for (const auto& entry : retries) {
            RetryPolicy value;
            DecodeRetryPolicy(entry.second, value);
            out.retries_.emplace(entry.first.Scalar(), std::move(value));
        }
    }
    DecodeStringList(node, "secrets", out.secrets_);
    if (Has(node, "timeouts")) {
        const Node timeouts = Child(node, "timeouts");
        RequireMap(timeouts, "use.timeouts");
        for (const auto& entry : timeouts) {
            Timeout value;
            DecodeTimeout(entry.second, value);
            out.timeouts_.emplace(entry.first.Scalar(), std::move(value));
        }
    }
    if (Has(node, "catalogs")) {
        const Node catalogs = Child(node, "catalogs");
        RequireMap(catalogs, "use.catalogs");
        for (const auto& entry : catalogs) {
            Catalog value;
            DecodeCatalog(entry.second, value);
            out.catalogs_.emplace(entry.first.Scalar(), std::move(value));
        }
    }
}

void DecodeDocumentInfo(const Node& node, DocumentInfo& out) {
    RequireMap(node, "document");
    RejectUnknownKeys(node, {"dsl", "namespace", "name", "version", "title", "summary", "tags",
                             "metadata"},
                      "document");
    DecodeRequired(node, "dsl", out.dsl_, "document");
    DecodeRequired(node, "namespace", out.namespace_, "document");
    DecodeRequired(node, "name", out.name_, "document");
    DecodeRequired(node, "version", out.version_, "document");
    DecodeOptional(node, "title", out.title_);
    DecodeOptional(node, "summary", out.summary_);
    if (Has(node, "tags")) {
        DecodeValue(Child(node, "tags"), out.tags_);
    }
    if (Has(node, "metadata")) {
        DecodeValue(Child(node, "metadata"), out.metadata_);
    }
}

void DecodeSchedule(const Node& node, Schedule& out) {
    RequireMap(node, "schedule");
    RejectUnknownKeys(node, {"every", "cron", "after", "on", "read"}, "schedule");
    if (Has(node, "every")) {
        DecodeDuration(Child(node, "every"), out.every_.emplace());
    }
    DecodeOptional(node, "cron", out.cron_);
    if (Has(node, "after")) {
        DecodeDuration(Child(node, "after"), out.after_.emplace());
    }
    if (Has(node, "on")) {
        out.on_.emplace();
        DecodeEventConsumptionStrategy(Child(node, "on"), *out.on_);
    }
    DecodeOptional(node, "read", out.read_);
}

void DecodeEvaluate(const Node& node, Evaluate& out) {
    RequireMap(node, "evaluate");
    RejectUnknownKeys(node, {"language", "mode"}, "evaluate");
    DecodeOptional(node, "language", out.language_);
    DecodeOptional(node, "mode", out.mode_);
}

void DecodeDocumentImpl(const Node& node, Document& out) {
    RequireMap(node, "workflow");
    RejectUnknownKeys(node, {"document", "input", "use", "do", "timeout", "output", "schedule",
                             "evaluate"},
                      "workflow");
    RequireKey(node, "document", "workflow");
    DecodeDocumentInfo(Child(node, "document"), out.document_);
    if (Has(node, "input")) {
        out.input_.emplace();
        DecodeInput(Child(node, "input"), *out.input_);
    }
    if (Has(node, "use")) {
        out.use_.emplace();
        DecodeUse(Child(node, "use"), *out.use_);
    }
    RequireKey(node, "do", "workflow");
    DecodeTasks(Child(node, "do"), out.do_);
    if (Has(node, "timeout")) {
        const Node timeout = Child(node, "timeout");
        if (timeout.IsScalar()) {
            out.timeout_ = timeout.as<std::string>();
        } else {
            Timeout value;
            DecodeTimeout(timeout, value);
            out.timeout_ = std::move(value);
        }
    }
    if (Has(node, "output")) {
        out.output_.emplace();
        DecodeOutput(Child(node, "output"), *out.output_);
    }
    if (Has(node, "schedule")) {
        out.schedule_.emplace();
        DecodeSchedule(Child(node, "schedule"), *out.schedule_);
    }
    if (Has(node, "evaluate")) {
        out.evaluate_.emplace();
        DecodeEvaluate(Child(node, "evaluate"), *out.evaluate_);
    }
}

} // namespace

auto DecodeDocument(const YAML::Node& node, Document& out) -> bool {
    DecodeDocumentImpl(node, out);
    return true;
}

} // namespace strij::openworkflow::parser

namespace YAML {

auto convert<strij::openworkflow::Document>::decode(const Node& node,
                                                    strij::openworkflow::Document& rhs) -> bool {
    return strij::openworkflow::parser::DecodeDocument(node, rhs);
}

} // namespace YAML
