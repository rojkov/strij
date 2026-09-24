#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "openworkflow/value.hh"

namespace strij::openworkflow {

// A duration is either an inline units object or an ISO-8601 / expression
// string. The parser never resolves or validates the string form.
struct DurationUnits {
    std::optional<std::int64_t> days_;
    std::optional<std::int64_t> hours_;
    std::optional<std::int64_t> minutes_;
    std::optional<std::int64_t> seconds_;
    std::optional<std::int64_t> milliseconds_;
};

using Duration = std::variant<DurationUnits, std::string>;

// An OAuth2 security token.
struct OAuth2Token {
    std::string token_;
    std::string type_;
};

// Inline configuration of an OAuth2 (or OIDC) authentication policy.
struct OAuth2AuthenticationProperties {
    struct Client {
        std::optional<std::string> id_;
        std::optional<std::string> secret_;
        std::optional<std::string> assertion_;
        std::optional<std::string> authentication_;
    };

    struct Request {
        std::optional<std::string> encoding_;
    };

    struct Endpoints {
        std::optional<std::string> token_;
        std::optional<std::string> introspection_;
    };

    std::string authority_;
    std::string grant_;
    std::optional<Client> client_;
    std::optional<Request> request_;
    std::optional<Endpoints> endpoints_;
    std::vector<std::string> issuers_;
    std::vector<std::string> scopes_;
    std::vector<std::string> audiences_;
    std::optional<std::string> username_;
    std::optional<std::string> password_;
    std::optional<OAuth2Token> subject_;
    std::optional<OAuth2Token> actor_;
};

struct BasicAuthentication {
    std::optional<std::string> username_;
    std::optional<std::string> password_;
    std::optional<std::string> use_;
};

struct BearerAuthentication {
    std::optional<std::string> token_;
    std::optional<std::string> use_;
};

struct DigestAuthentication {
    std::optional<std::string> username_;
    std::optional<std::string> password_;
    std::optional<std::string> use_;
};

struct OAuth2Authentication {
    std::optional<OAuth2AuthenticationProperties> properties_;
    std::optional<std::string> use_;
};

struct OidcAuthentication {
    std::optional<OAuth2AuthenticationProperties> properties_;
    std::optional<std::string> use_;
};

// A named reference to a reusable authentication policy.
struct AuthenticationReference {
    std::string use_;
};

// A discriminated authentication policy: exactly one scheme, or a named
// reference to one declared under `use.authentications`.
struct Authentication {
    using Scheme = std::variant<AuthenticationReference, BasicAuthentication, BearerAuthentication,
                                DigestAuthentication, OAuth2Authentication, OidcAuthentication>;
    Scheme scheme_;
};

struct EndpointObject {
    std::string uri_;
    std::optional<Authentication> authentication_;
};

// An endpoint is either a uri/expression string or an object with a uri and an
// optional authentication policy.
struct Endpoint {
    std::variant<std::string, EndpointObject> value_;
};

struct ExternalResource {
    std::optional<std::string> name_;
    Endpoint endpoint_;
};

// A schema is either an inline document or an external resource reference.
struct Schema {
    std::string format_ = "json";
    std::variant<Value, ExternalResource> source_;
};

struct Input {
    std::optional<Schema> schema_;
    std::optional<std::variant<std::string, Value>> from_;
};

struct Output {
    std::optional<Schema> schema_;
    std::optional<std::variant<std::string, Value>> as_;
};

struct Export {
    std::optional<Schema> schema_;
    std::optional<std::variant<std::string, Value>> as_;
};

struct Error {
    std::string type_;
    std::int64_t status_ = 0;
    std::optional<std::string> instance_;
    std::optional<std::string> title_;
    std::optional<std::string> detail_;
};

struct ErrorFilter {
    std::optional<std::string> type_;
    std::optional<std::int64_t> status_;
    std::optional<std::string> instance_;
    std::optional<std::string> title_;
    std::optional<std::string> detail_;
};

// A retry backoff branch. The reference enumerates no inner keys, so each
// branch's payload is kept opaque until a concrete schema appears.
struct Backoff {
    std::string kind_;
    Value parameters_;
};

struct Jitter {
    Duration from_;
    Duration to_;
};

struct RetryLimit {
    struct Attempt {
        std::optional<std::int64_t> count_;
        std::optional<Duration> duration_;
    };

    std::optional<Attempt> attempt_;
    std::optional<Duration> duration_;
};

struct RetryPolicy {
    std::optional<std::string> when_;
    std::optional<std::string> exceptWhen_;
    std::optional<Duration> delay_;
    std::optional<Backoff> backoff_;
    std::optional<RetryLimit> limit_;
    std::optional<Jitter> jitter_;
};

struct Catalog {
    Endpoint endpoint_;
};

struct Timeout {
    Duration after_;
};

} // namespace strij::openworkflow
