#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "examples.hh"
#include "gtest/gtest.h"
#include "openworkflow/document.hh"
#include "openworkflow/parser/parse.hh"
#include "openworkflow/task.hh"

namespace strij::openworkflow {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

template <typename Body>
auto GetBody(const TaskBody& body) -> const Body& {
    return std::get<Body>(body);
}

auto ParseOrDie(std::string_view text) -> Document {
    auto result = Parse(text);
    EXPECT_TRUE(result.has_value()) << result.error().Message();
    return result.value();
}

// ---------------------------------------------------------------------------
// Golden tests: every reference example parses (task 4.1)
// ---------------------------------------------------------------------------

TEST(GoldenTest, ReferenceExamplesParse) {
    const std::vector<std::pair<std::string_view, std::string_view>> examples = {
        {"do-single", test::kDoSingle},
        {"do-multiple", test::kDoMultiple},
        {"for", test::kFor},
        {"for-inline-array", test::kForInlineArray},
        {"for-inline-primitive-array", test::kForInlinePrimitiveArray},
        {"fork", test::kFork},
        {"try-catch", test::kTryCatch},
        {"try-catch-retry-inline", test::kTryCatchRetryInline},
        {"try-catch-then", test::kTryCatchThen},
        {"try-catch-then-directive", test::kTryCatchThenDirective},
        {"switch-then-string", test::kSwitchThenString},
        {"set", test::kSet},
        {"set-expression", test::kSetExpression},
        {"wait-duration-inline", test::kWaitInline},
        {"wait-duration-iso8601", test::kWaitIso8601},
        {"emit", test::kEmit},
        {"raise-inline", test::kRaiseInline},
        {"raise-reusable", test::kRaiseReusable},
        {"run-container", test::kRunContainer},
        {"run-container-cleanup-always", test::kRunContainerCleanupAlways},
        {"run-container-cleanup-eventually", test::kRunContainerCleanupEventually},
        {"run-container-stdin-and-arguments", test::kRunContainerStdinArguments},
        {"run-container-with-name", test::kRunContainerWithName},
        {"run-container-with-pull-policy", test::kRunContainerPullPolicy},
        {"run-return-all", test::kRunReturnAll},
        {"run-return-code", test::kRunReturnCode},
        {"run-return-none", test::kRunReturnNone},
        {"run-return-stderr", test::kRunReturnStderr},
        {"run-script-with-stdin-and-arguments", test::kRunScript},
        {"run-shell-stdin-and-arguments", test::kRunShell},
        {"run-subflow", test::kRunSubflow},
        {"call-http-endpoint-interpolation", test::kCallHttpInterpolation},
        {"call-http-endpoint-interpolation-shorthand", test::kCallHttpInterpolationShorthand},
        {"call-http-query-headers-expressions", test::kCallHttpQueryHeaders},
        {"call-http-query-parameters", test::kCallHttpQueryParameters},
        {"call-http-redirect", test::kCallHttpRedirect},
        {"call-grpc", test::kCallGrpc},
        {"call-openapi", test::kCallOpenapi},
        {"call-openapi-redirect", test::kCallOpenapiRedirect},
        {"call-custom-function-inline", test::kCallCustomFunctionInline},
        {"call-custom-function-cataloged", test::kCallCustomFunctionCataloged},
        {"call-asyncapi-publish", test::kCallAsyncapiPublish},
        {"call-asyncapi-consume-amount", test::kCallAsyncapiConsumeAmount},
        {"call-asyncapi-consume-until", test::kCallAsyncapiConsumeUntil},
        {"call-asyncapi-consume-while", test::kCallAsyncapiConsumeWhile},
        {"call-asyncapi-consume-forever", test::kCallAsyncapiConsumeForever},
        {"call-mcp", test::kCallMcp},
        {"authentication-oauth2", test::kAuthenticationOAuth2},
        {"authentication-oauth2-secret", test::kAuthenticationOAuth2Secret},
        {"authentication-oidc", test::kAuthenticationOidc},
        {"authentication-oidc-secret", test::kAuthenticationOidcSecret},
        {"authentication-bearer", test::kAuthenticationBearer},
        {"authentication-bearer-uri-format", test::kAuthenticationBearerUriFormat},
        {"authentication-reusable", test::kAuthenticationReusable},
        {"mock-service-extension", test::kMockServiceExtension},
        {"schedule-cron", test::kScheduleCron},
        {"schedule-event-driven", test::kScheduleEventDriven},
        {"listen-to-all", test::kListenAll},
        {"listen-to-all-read-envelope", test::kListenAllReadEnvelope},
        {"listen-to-one", test::kListenOne},
        {"listen-to-any", test::kListenAny},
        {"listen-to-any-filter", test::kListenAnyFilter},
        {"listen-to-any-until-condition", test::kListenAnyUntilCondition},
        {"listen-to-any-until-consumed", test::kListenAnyUntilConsumed},
        {"listen-to-any-forever-foreach", test::kListenAnyForeverForeach},
        {"star-wars-homeworld", test::kStarWarsHomeworld},
        {"accumulate-room-readings", test::kAccumulateRoomReadings},
        {"conditional-task", test::kConditionalTask},
    };

    for (const auto& [name, text] : examples) {
        SCOPED_TRACE(name);
        auto result = Parse(text);
        ASSERT_TRUE(result.has_value()) << result.error().Message();
    }
}

// ---------------------------------------------------------------------------
// Document shape (task 4.1)
// ---------------------------------------------------------------------------

TEST(DocumentTest, MinimalDocumentExposesMetadataAndTasks) {
    const auto result = Parse(R"YAML(
document:
  dsl: '1.0.3'
  namespace: examples
  name: hello
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        method: get
)YAML");
    ASSERT_TRUE(result.has_value()) << result.error().Message();
    EXPECT_EQ(result->document_.dsl_, "1.0.3");
    EXPECT_EQ(result->document_.namespace_, "examples");
    EXPECT_EQ(result->document_.name_, "hello");
    EXPECT_EQ(result->document_.version_, "0.1.0");
    ASSERT_EQ(result->do_.size(), 1U);
    EXPECT_EQ(result->do_[0].name_, "getPet");
    const auto& body = GetBody<CallBody>(result->do_[0].task_.body_);
    EXPECT_EQ(body.call_, "http");
}

TEST(DocumentTest, AllOptionalSectionsParse) {
    const auto result = Parse(R"YAML(
document:
  dsl: '1.0.3'
  namespace: examples
  name: sections
  version: '0.1.0'
  title: A title
  summary: A summary
  tags:
    team: core
  metadata:
    owner: someone
input:
  schema:
    format: json
    document:
      type: object
timeout:
  after:
    hours: 1
output:
  as: '${ . }'
schedule:
  every:
    minutes: 5
  read: envelope
evaluate:
  language: jq
  mode: strict
do:
  - noop:
      set:
        done: true
)YAML");
    ASSERT_TRUE(result.has_value()) << result.error().Message();
    EXPECT_TRUE(result->input_.has_value());
    EXPECT_TRUE(result->timeout_.has_value());
    EXPECT_TRUE(result->output_.has_value());
    EXPECT_TRUE(result->schedule_.has_value());
    EXPECT_TRUE(result->evaluate_.has_value());
    EXPECT_EQ(result->evaluate_->mode_, "strict");
    ASSERT_TRUE(result->schedule_->every_.has_value());
    EXPECT_TRUE(std::holds_alternative<DurationUnits>(*result->schedule_->every_));
}

// ---------------------------------------------------------------------------
// Task collections (task 4.3)
// ---------------------------------------------------------------------------

TEST(TaskCollectionTest, SequenceFormPreservesOrderAndNames) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - first:
      set: {a: 1}
  - second:
      set: {a: 2}
  - third:
      set: {a: 3}
)YAML");
    ASSERT_EQ(doc.do_.size(), 3U);
    EXPECT_EQ(doc.do_[0].name_, "first");
    EXPECT_EQ(doc.do_[1].name_, "second");
    EXPECT_EQ(doc.do_[2].name_, "third");
}

TEST(TaskCollectionTest, MapFormIsAccepted) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  first:
    set: {a: 1}
  second:
    set: {a: 2}
)YAML");
    ASSERT_EQ(doc.do_.size(), 2U);
    EXPECT_EQ(doc.do_[0].name_, "first");
    EXPECT_EQ(doc.do_[1].name_, "second");
}

// ---------------------------------------------------------------------------
// One-of discriminators (task 4.3)
// ---------------------------------------------------------------------------

TEST(OneOfTest, MultipleTaskKindsFail) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - bad:
      call: http
      wait: PT1S
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("call"), std::string::npos);
    EXPECT_NE(result.error().Message().find("wait"), std::string::npos);
    EXPECT_LT(result.error().Position(), text.size());
}

TEST(OneOfTest, NoTaskKindFails) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - bad:
      if: true
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("task kind"), std::string::npos);
}

TEST(OneOfTest, ValidRunProcessKindsParse) {
    for (const std::string_view process : {
             "container: {image: alpine}",
             "shell: {command: echo hi}",
             "script: {language: js, code: 'x'}",
             "workflow: {namespace: n, name: w, version: '0.1.0'}",
         }) {
        SCOPED_TRACE(process);
        const auto doc = ParseOrDie("document: {dsl: '1.0.3', namespace: n, name: w, version: "
                                    "'0.1.0'}\ndo:\n  - runIt:\n      run:\n        " +
                                    std::string(process) + "\n");
        EXPECT_TRUE(std::holds_alternative<RunBody>(doc.do_[0].task_.body_));
    }
}

TEST(OneOfTest, MultipleRunProcessesFail) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - bad:
      run:
        container: {image: alpine}
        shell: {command: echo hi}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("mutually exclusive"), std::string::npos);
}

TEST(OneOfTest, MultipleAuthenticationSchemesFail) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
use:
  authentications:
    bad:
      basic: {username: u, password: p}
      bearer: {token: t}
do:
  - noop:
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("mutually exclusive"), std::string::npos);
}

TEST(OneOfTest, MultipleSchemaSourcesFail) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
input:
  schema:
    document: {type: object}
    resource: {endpoint: https://example.com/schema.json}
do:
  - noop:
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("mutually exclusive"), std::string::npos);
}

TEST(OneOfTest, MultipleConsumptionStrategiesFail) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
schedule:
  on:
    all: []
    one:
      with: {type: t}
do:
  - noop:
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("mutually exclusive"), std::string::npos);
}

TEST(OneOfTest, MultipleBackoffBranchesFail) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
use:
  retries:
    bad:
      backoff:
        constant: {}
        linear: {}
do:
  - noop:
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("mutually exclusive"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Raw preservation (task 4.4)
// ---------------------------------------------------------------------------

TEST(RawPreservationTest, BareAndWrappedExpressionsBothParse) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - choose:
      switch:
        - wrapped:
            when: '${ .a > 1 }'
            then: end
        - bare:
            when: .b == "x"
            then: end
)YAML");
    const auto& body = GetBody<SwitchBody>(doc.do_[0].task_.body_);
    ASSERT_EQ(body.cases_.size(), 2U);
    EXPECT_EQ(body.cases_[0].when_, "${ .a > 1 }");
    EXPECT_EQ(body.cases_[1].when_, ".b == \"x\"");
}

TEST(RawPreservationTest, DurationAcceptsBothForms) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - iso:
      wait: PT30S
  - units:
      wait:
        seconds: 30
)YAML");
    const auto& iso = GetBody<WaitBody>(doc.do_[0].task_.body_);
    ASSERT_TRUE(std::holds_alternative<std::string>(iso.wait_));
    EXPECT_EQ(std::get<std::string>(iso.wait_), "PT30S");

    const auto& units = GetBody<WaitBody>(doc.do_[1].task_.body_);
    ASSERT_TRUE(std::holds_alternative<DurationUnits>(units.wait_));
    const auto& duration = std::get<DurationUnits>(units.wait_);
    ASSERT_TRUE(duration.seconds_.has_value());
    EXPECT_EQ(*duration.seconds_, 30);
}

TEST(RawPreservationTest, ReferenceFieldsStayUnresolved) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
use:
  timeouts:
    fast:
      after: {seconds: 1}
do:
  - byName:
      timeout: fast
      wait: PT1S
  - inline:
      timeout:
        after: {seconds: 2}
      wait: PT1S
  - reusable:
      raise:
        error: notImplemented
)YAML");
    const auto& named = doc.do_[0].task_.timeout_;
    ASSERT_TRUE(named.has_value());
    ASSERT_TRUE(std::holds_alternative<std::string>(*named));
    EXPECT_EQ(std::get<std::string>(*named), "fast");

    const auto& inline_timeout = doc.do_[1].task_.timeout_;
    ASSERT_TRUE(inline_timeout.has_value());
    EXPECT_TRUE(std::holds_alternative<Timeout>(*inline_timeout));

    const auto& raise = GetBody<RaiseBody>(doc.do_[2].task_.body_);
    ASSERT_TRUE(std::holds_alternative<std::string>(raise.error_));
    EXPECT_EQ(std::get<std::string>(raise.error_), "notImplemented");
}

// ---------------------------------------------------------------------------
// Opaque fidelity (task 4.5)
// ---------------------------------------------------------------------------

TEST(OpaqueFidelityTest, MixedScalarKindsRoundTrip) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - configure:
      metadata:
        enabled: true
        retries: 3
        ratio: 0.5
        label: hello
        nested:
          items:
            - 1
            - two
      set: {configured: true}
)YAML");
    const Value& metadata = doc.do_[0].task_.metadata_;
    ASSERT_TRUE(metadata.IsObject());
    const auto& object = metadata.AsObject();
    EXPECT_TRUE(object.at("enabled").IsBool());
    EXPECT_TRUE(object.at("retries").IsInt());
    EXPECT_TRUE(object.at("ratio").IsDouble());
    EXPECT_TRUE(object.at("label").IsString());
    ASSERT_TRUE(object.at("nested").IsObject());
    const auto& items = object.at("nested").AsObject().at("items");
    ASSERT_TRUE(items.IsArray());
    EXPECT_TRUE(items.AsArray()[0].IsInt());
    EXPECT_TRUE(items.AsArray()[1].IsString());
}

TEST(OpaqueFidelityTest, CallArgumentsAreKeptOpaque) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - getPet:
      call: http
      with:
        method: get
        count: 2
        tags:
          - a
          - b
)YAML");
    const auto& body = GetBody<CallBody>(doc.do_[0].task_.body_);
    ASSERT_TRUE(body.with_.IsObject());
    EXPECT_EQ(body.with_.AsObject().at("method").AsString(), "get");
    EXPECT_TRUE(body.with_.AsObject().at("count").IsInt());
    EXPECT_TRUE(body.with_.AsObject().at("tags").IsArray());
}

// ---------------------------------------------------------------------------
// Deferred semantics (task 4.6)
// ---------------------------------------------------------------------------

TEST(DeferredSemanticsTest, QuestionableInputStillParses) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
schedule:
  read: data
do:
  - runIt:
      run:
        container: {image: alpine}
        return: banana
      then: does-not-exist
  - callIt:
      call: thisFunctionDoesNotExist
      with: {a: 1}
  - listenIt:
      listen:
        to:
          any: []
          until: '${ false and notClosed }'
)YAML");
    const auto& run = GetBody<RunBody>(doc.do_[0].task_.body_);
    ASSERT_TRUE(run.return_.has_value());
    EXPECT_EQ(*run.return_, "banana");
    ASSERT_TRUE(doc.do_[0].task_.then_.has_value());
    EXPECT_EQ(*doc.do_[0].task_.then_, "does-not-exist");
    EXPECT_EQ(GetBody<CallBody>(doc.do_[1].task_.body_).call_, "thisFunctionDoesNotExist");
    const auto& listen = GetBody<ListenBody>(doc.do_[2].task_.body_);
    ASSERT_TRUE(listen.to_.until_.has_value());
}

// ---------------------------------------------------------------------------
// Unknown keys (task 4.7)
// ---------------------------------------------------------------------------

TEST(UnknownKeyTest, UnknownWorkflowKeyFails) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
bogus: true
do:
  - noop:
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("bogus"), std::string::npos);
}

TEST(UnknownKeyTest, TypoTaskKindFailsNamingTheKey) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - bad:
      dok:
        - nested:
            set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("dok"), std::string::npos);
}

TEST(UnknownKeyTest, UnknownComponentKeyFails) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
use:
  timeouts:
    fast:
      after: {seconds: 1}
      nope: true
do:
  - noop:
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("nope"), std::string::npos);
}

TEST(UnknownKeyTest, UnknownSubObjectKeyFails) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - noop:
      input:
        bogus: true
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("bogus"), std::string::npos);
}

TEST(UnknownKeyTest, MetadataAcceptsAnything) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
do:
  - noop:
      metadata:
        anything: [1, 2, {three: 3.0}]
      set: {a: 1}
)YAML");
    EXPECT_TRUE(doc.do_[0].task_.metadata_.IsObject());
}

// ---------------------------------------------------------------------------
// Reusable components (spec: all use categories parse)
// ---------------------------------------------------------------------------

TEST(UseTest, AllUseCategoriesParse) {
    const auto doc = ParseOrDie(R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
use:
  authentications:
    auth1:
      basic: {username: u, password: p}
  errors:
    err1:
      type: https://example.com/error
      status: 500
  timeouts:
    t1:
      after: {seconds: 5}
  retries:
    r1:
      delay: {seconds: 1}
      backoff:
        exponential: {}
  catalogs:
    c1:
      endpoint: https://example.com/catalog
  extensions:
    - ext1:
        extend: call
        before:
          - prep:
              set: {ready: true}
  secrets:
    - SECRET_A
    - SECRET_B
  functions:
    f1:
      call: http
      with: {method: get}
do:
  - noop:
      set: {a: 1}
)YAML");
    ASSERT_TRUE(doc.use_.has_value());
    const Use& use = *doc.use_;
    EXPECT_EQ(use.authentications_.size(), 1U);
    EXPECT_EQ(use.errors_.size(), 1U);
    EXPECT_EQ(use.timeouts_.size(), 1U);
    EXPECT_EQ(use.retries_.size(), 1U);
    EXPECT_EQ(use.catalogs_.size(), 1U);
    ASSERT_EQ(use.extensions_.size(), 1U);
    EXPECT_EQ(use.extensions_[0].name_, "ext1");
    EXPECT_EQ(use.extensions_[0].extension_.before_.size(), 1U);
    EXPECT_EQ(use.secrets_.size(), 2U);
    EXPECT_EQ(use.functions_.size(), 1U);
}

// ---------------------------------------------------------------------------
// Error contract (task 4.2)
// ---------------------------------------------------------------------------

TEST(ParseErrorContractTest, SyntaxErrorCarriesPosition) {
    const std::string_view text = "document: [unclosed\n";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().Message().empty());
    EXPECT_LE(result.error().Position(), text.size());
}

TEST(ParseErrorContractTest, MissingRequiredDocumentKeyFails) {
    const std::string_view text = R"YAML(
do:
  - noop:
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("document"), std::string::npos);
    EXPECT_LT(result.error().Position(), text.size());
}

TEST(ParseErrorContractTest, MissingRequiredDoKeyFails) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w, version: '0.1.0'}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("do"), std::string::npos);
}

TEST(ParseErrorContractTest, MissingDocumentFieldNamesConstruct) {
    const std::string_view text = R"YAML(
document: {dsl: '1.0.3', namespace: n, name: w}
do:
  - noop:
      set: {a: 1}
)YAML";
    const auto result = Parse(text);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().Message().find("version"), std::string::npos);
    EXPECT_NE(result.error().Message().find("document"), std::string::npos);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::openworkflow
