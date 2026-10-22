#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

#include "common/task/task.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"
#include "openworkflow/document.hh"
#include "openworkflow/parser/parse.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

using ::strij::openworkflow::Document;
using ::strij::openworkflow::NamedTask;

// NOLINTBEGIN(modernize-use-trailing-return-type)

constexpr std::string_view kSingleStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: single
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kTwoSteps = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: two
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
  - two:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kThreeSteps = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: three
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
  - two:
      call: strij
      with:
        type: echo
  - three:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kTwoStepsWithExpression = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: expr
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
  - two:
      call: strij
      with:
        type: echo
        parameters:
          p: '${ .field }'
)YAML";

constexpr std::string_view kUnknownKey = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bad
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
        bogus: value
)YAML";

constexpr std::string_view kBadRequirement = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bad
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
        requirements:
          cpu: 1.5
)YAML";

constexpr std::string_view kPartialWrapper = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: partial
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
        parameters:
          p: 'jobs/${ .id }'
)YAML";

constexpr std::string_view kNestedMalformed = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
        parameters:
          outer:
            inner: '${ .x( }'
)YAML";

constexpr std::string_view kReservedTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: reserved
  version: '0.1.0'
do:
  - one:
      call: http
      with:
        method: get
)YAML";

constexpr std::string_view kUnknownTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: unknown
  version: '0.1.0'
do:
  - one:
      call: bogus
      with:
        type: echo
)YAML";

constexpr std::string_view kDeclaredFunction = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: function
  version: '0.1.0'
use:
  functions:
    getPet:
      call: strij
      with:
        type: echo
do:
  - one:
      call: getPet
      with:
        type: echo
)YAML";

constexpr std::string_view kLooseMode = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: loose
  version: '0.1.0'
evaluate:
  mode: loose
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kSeedInput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: seed
  version: '0.1.0'
input:
  from: '${ {"greeting":"hello"} }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kIdentityStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: identity
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: ${ . }
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentInputFromBody = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: from-body
  version: '0.1.0'
input:
  from: '${ .input }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentInputIdentity = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: input-identity
  version: '0.1.0'
input:
  from: '${ . }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedDo = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-do
  version: '0.1.0'
do:
  - outer:
      do:
        - a:
            call: strij
            with:
              type: echo
        - b:
            call: strij
            with:
              type: echo
  - c:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForkThreeBranches = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-all
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
          - b:
              call: strij
              with:
                type: echo
          - c:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkTwoBranches = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-two
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
          - b:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkJoinAllThenStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-then-step
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
          - b:
              call: strij
              with:
                type: echo
  - after:
      call: strij
      with:
        type: echo
        parameters:
          p: '${ .[1].b }'
)YAML";

constexpr std::string_view kForkCompete = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-compete
  version: '0.1.0'
do:
  - race:
      fork:
        compete: true
        branches:
          - fast:
              call: strij
              with:
                type: echo
          - slow:
              call: strij
              with:
                type: echo
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForkNested = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-nested
  version: '0.1.0'
do:
  - outer:
      fork:
        branches:
          - nested:
              do:
                - inner:
                    fork:
                      compete: true
                      branches:
                        - x:
                            call: strij
                            with:
                              type: echo
                        - y:
                            call: strij
                            with:
                              type: echo
          - plain:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkCompeteAbandonsNested = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-compete-nested
  version: '0.1.0'
do:
  - race:
      fork:
        compete: true
        branches:
          - nested:
              do:
                - inner:
                    fork:
                      branches:
                        - x:
                            do:
                              - x1:
                                  call: strij
                                  with:
                                    type: echo
                              - x2:
                                  call: strij
                                  with:
                                    type: echo
                        - y:
                            do:
                              - y1:
                                  call: strij
                                  with:
                                    type: echo
                              - y2:
                                  call: strij
                                  with:
                                    type: echo
          - plain:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkMultiStepBranch = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-multi-step
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - chained:
              do:
                - first:
                    call: strij
                    with:
                      type: echo
                - second:
                    call: strij
                    input:
                      from: ${ . }
                    with:
                      type: echo
                      parameters:
                        p: '${ .value }'
          - plain:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkNestedDoFailure = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-nested-do-failure
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - chained:
              do:
                - inner:
                    call: strij
                    with:
                      type: echo
          - plain:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkNestedJoinFailure = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-nested-join-failure
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - outer:
              fork:
                branches:
                  - leaf:
                      call: strij
                      with:
                        type: echo
)YAML";

constexpr std::string_view kBranchUnknownTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: branch-unknown
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: bogus
              with:
                type: echo
          - b:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kEmptyJoinAllFork = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: empty-all
  version: '0.1.0'
do:
  - fan:
      fork:
        branches: []
)YAML";

constexpr std::string_view kEmptyCompetingFork = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: empty-compete
  version: '0.1.0'
do:
  - fan:
      fork:
        compete: true
        branches: []
)YAML";

constexpr std::string_view kDocumentInputString = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: input-string
  version: '0.1.0'
input:
  from: '${ .name }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kPlainStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: plain
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kCallInputFromWithExpression = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: call-input
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: '${ .payload }'
      with:
        type: echo
        parameters:
          p: '${ .id }'
)YAML";

constexpr std::string_view kCallInputFromString = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: call-input-string
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: '${ .name }'
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedDoInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-input
  version: '0.1.0'
do:
  - outer:
      input:
        from: '${ .payload }'
      do:
        - a:
            call: strij
            with:
              type: echo
              parameters:
                p: '${ .id }'
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForkInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-input
  version: '0.1.0'
do:
  - fan:
      input:
        from: '${ .payload }'
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
                parameters:
                  p: '${ .id }'
          - b:
              call: strij
              with:
                type: echo
                parameters:
                  p: '${ .id }'
)YAML";

constexpr std::string_view kConditionFalseSkip = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-false
  version: '0.1.0'
do:
  - maybe:
      if: '${ .enabled }'
      then: exit
      output:
        as: '${ 1 }'
      call: strij
      with:
        type: echo
        parameters:
          p: '${ .id }'
  - next:
      call: strij
      with:
        type: echo
        parameters:
          p: '${ .id }'
)YAML";

constexpr std::string_view kConditionTrue = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-true
  version: '0.1.0'
do:
  - one:
      if: '${ .enabled }'
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kConditionNestedDoSkip = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-nested
  version: '0.1.0'
do:
  - outer:
      do:
        - a:
            if: '${ .enabled }'
            call: strij
            with:
              type: echo
        - b:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kConditionForkBranchSkip = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-fork
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              if: '${ .enabled }'
              call: strij
              with:
                type: echo
          - b:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kConditionNonBoolean = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-number
  version: '0.1.0'
do:
  - one:
      if: '${ .count }'
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kConditionOpaqueSubject = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-opaque
  version: '0.1.0'
do:
  - one:
      if: '${ .enabled }'
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kBareIf = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-if
  version: '0.1.0'
do:
  - one:
      if: .enabled
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedIf = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-if
  version: '0.1.0'
do:
  - one:
      if: '${ .enabled( }'
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kBareInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-input
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: .payload
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-input
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: '${ .payload( }'
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedBareInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-bare-input
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from:
          id: .payload.id
      with:
        type: echo
)YAML";

constexpr std::string_view kBareOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-output
  version: '0.1.0'
do:
  - one:
      call: strij
      output:
        as: .result
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-output
  version: '0.1.0'
do:
  - one:
      call: strij
      output:
        as: '${ .result( }'
      with:
        type: echo
)YAML";

constexpr std::string_view kBareDocumentOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-document-output
  version: '0.1.0'
output:
  as: .result
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedDocumentOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-document-output
  version: '0.1.0'
output:
  as: '${ .result( }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kCallOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: call-output
  version: '0.1.0'
do:
  - one:
      call: strij
      output:
        as: '${ .id }'
      with:
        type: echo
  - two:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kDoOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: do-output
  version: '0.1.0'
do:
  - outer:
      output:
        as: '${ .id }'
      do:
        - inner:
            call: strij
            with:
              type: echo
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForkOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-output
  version: '0.1.0'
do:
  - fan:
      output:
        as: '${ .[0].id }'
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kOutputAsUsesInputAndOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: output-vars
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: '${ {seed: .seed} }'
      output:
        as: '${ {before: $input, after: $output} }'
      with:
        type: echo
  - two:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kOutputAsOpaqueOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: opaque-output
  version: '0.1.0'
do:
  - one:
      call: strij
      output:
        as: '${ . }'
      with:
        type: echo
)YAML";

constexpr std::string_view kOutputAsOpaqueInput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: opaque-input
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
  - two:
      call: strij
      output:
        as: '${ . }'
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: document-output
  version: '0.1.0'
output:
  as: '${ {petId: .id} }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentOutputOpaque = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: document-output-opaque
  version: '0.1.0'
output:
  as: '${ .id }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentInputObservesEmptyContext = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: initial-context
  version: '0.1.0'
input:
  from: '${ {context: $context} }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kNoExportLeavesContextEmpty = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: empty-context
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kConditionAndParameterReadContext = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: live-context
  version: '0.1.0'
do:
  - seed:
      call: strij
      export:
        as: '${ {token: "abc", enabled: true} }'
      with:
        type: echo
  - gated:
      if: '${ $context.enabled }'
      call: strij
      with:
        type: echo
        parameters:
          p: '${ $context.token }'
)YAML";

constexpr std::string_view kExportMerge = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-merge
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - seed:
      call: strij
      export:
        as: '${ {a: 1} }'
      with:
        type: echo
  - merge:
      call: strij
      export:
        as: '${ $context + {b: 2} }'
      with:
        type: echo
)YAML";

constexpr std::string_view kExportReplace = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-replace
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - seed:
      call: strij
      export:
        as: '${ {a: 1} }'
      with:
        type: echo
  - replace:
      call: strij
      export:
        as: '${ . }'
      with:
        type: echo
)YAML";

constexpr std::string_view kExportUsesInputAndOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-vars
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - one:
      call: strij
      input:
        from: '${ {seed: .seed} }'
      output:
        as: '${ {after: .name} }'
      export:
        as: '${ {before: $input, after: $output} }'
      with:
        type: echo
)YAML";

constexpr std::string_view kExportAfterNoExportTask = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-then-plain
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - seed:
      call: strij
      export:
        as: '${ {a: 1} }'
      with:
        type: echo
  - plain:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kExplicitEmptyExport = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: empty-export
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - seed:
      call: strij
      export:
        as: '${ {a: 1} }'
      with:
        type: echo
  - noop:
      call: strij
      export: {}
      with:
        type: echo
)YAML";

constexpr std::string_view kExportSkippedTask = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-skipped
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - seed:
      call: strij
      export:
        as: '${ {a: 1} }'
      with:
        type: echo
  - skipped:
      if: '${ false }'
      call: strij
      export:
        as: '${ {b: 2} }'
      with:
        type: echo
)YAML";

constexpr std::string_view kExportFailedChild = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-failed
  version: '0.1.0'
do:
  - one:
      call: strij
      export:
        as: '${ {a: 1} }'
      with:
        type: echo
)YAML";

constexpr std::string_view kDoExport = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: do-export
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - outer:
      output:
        as: '${ {id: .id} }'
      export:
        as: '${ {context: $output} }'
      do:
        - inner:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kForkExport = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-export
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - fan:
      output:
        as: '${ .[0].id }'
      export:
        as: '${ {joined: $output} }'
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kExportOpaqueOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-opaque-output
  version: '0.1.0'
do:
  - one:
      call: strij
      export:
        as: '${ {a: 1} }'
      with:
        type: echo
)YAML";

constexpr std::string_view kExportOpaqueInput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-opaque-input
  version: '0.1.0'
do:
  - one:
      call: strij
      export:
        as: '${ {a: 1} }'
      with:
        type: echo
)YAML";

constexpr std::string_view kExportNonObject = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-non-object
  version: '0.1.0'
do:
  - one:
      call: strij
      export:
        as: '${ .value }'
      with:
        type: echo
)YAML";

constexpr std::string_view kExportInlineExactlyOnce = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: export-inline
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - one:
      call: strij
      export:
        as: '${ {count: (($context.count // 0) + 1)} }'
      with:
        type: echo
)YAML";

constexpr std::string_view kBareExportAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-export
  version: '0.1.0'
do:
  - one:
      call: strij
      export:
        as: .result
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedExportAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-export
  version: '0.1.0'
do:
  - one:
      call: strij
      export:
        as: '${ .result( }'
      with:
        type: echo
)YAML";

constexpr std::string_view kExportInsideForkBranch = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: branch-export
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: strij
              export:
                as: '${ {a: 1} }'
              with:
                type: echo
)YAML";

constexpr std::string_view kExportInsideNestedDoInBranch = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: branch-nested-export
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              do:
                - inner:
                    call: strij
                    export:
                      as: '${ {a: 1} }'
                    with:
                      type: echo
)YAML";

constexpr std::string_view kExportInsideNestedForkInBranch = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: branch-nested-fork-export
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              fork:
                branches:
                  - leaf:
                      call: strij
                      export:
                        as: '${ {a: 1} }'
                      with:
                        type: echo
)YAML";

struct Harness {
  explicit Harness(std::size_t for_iteration_budget = kDefaultSynchronousIterationBudget) {
    interpreter_ = std::make_shared<WorkflowInterpreter>(
        submitter_, test::JqEvaluatorConfig(), "parent",
        [this](task::TaskResult result) { results_.push_back(std::move(result)); },
        for_iteration_budget);
  }

  test::FakeChildTaskSubmitter submitter_;
  std::vector<task::TaskResult> results_;
  std::shared_ptr<WorkflowInterpreter> interpreter_;
};

TEST(InterpreterTest, RunsThreeStepsInDeclarationOrder) {
  Harness harness;
  auto counter = std::make_shared<int>(0);
  harness.submitter_.SetOnSubmit(
      [counter](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
        const std::string body = std::to_string(++*counter);
        const auto data = std::as_bytes(std::span(body.data(), body.size()));
        receiver.Deliver(data, true);
      });

  harness.interpreter_->Start(std::string(kThreeSteps));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "3");
  EXPECT_TRUE(harness.results_[0].is_final());
  EXPECT_EQ(*counter, 3);
}

TEST(InterpreterTest, LocalChildCompletingInsideSubmitAdvancesExactlyOnce) {
  Harness harness;
  harness.submitter_.SetOnSubmit([](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
    const std::string body = "done";
    const auto data = std::as_bytes(std::span(body.data(), body.size()));
    receiver.Deliver(data, true);
  });

  harness.interpreter_->Start(std::string(kSingleStep));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "done");
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterTest, SuspendsAcrossChildResultAndResumesAtFollowingStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kTwoSteps));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccess(harness.submitter_, "1");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccess(harness.submitter_, "2");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "2");
}

TEST(InterpreterTest, NestedDoRunsAtItsPositionAndResumesAfterIt) {
  Harness harness;
  harness.interpreter_->Start(std::string(kNestedDo));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  ASSERT_EQ(harness.submitter_.size(), 3U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 2, "3");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "3");
}

TEST(InterpreterTest, IgnoresOutcomeForUnknownChildId) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSingleStep));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  ChildOutcome stale;
  stale.body_ = "stale";
  harness.interpreter_->OnChildOutcome("some-other-id", std::move(stale));

  EXPECT_TRUE(harness.results_.empty());
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterTest, ChildFailureAfterSuspensionReportsOnceAndStops) {
  Harness harness;
  harness.interpreter_->Start(std::string(kTwoSteps));
  test::CompleteError(harness.submitter_, "refused", task::TASK_STATUS_CAPACITY_REFUSED);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_TRUE(harness.results_[0].is_final());
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterTest, SeedInputExpressionBecomesFirstChildBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSeedInput));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"greeting":"hello"})");
}

TEST(InterpreterTest, InvocationBodySeedsFirstChildBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kIdentityStep), R"({"input":5})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"input":5})");
}

TEST(InterpreterTest, DocumentInputTransformsInvocationBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentInputFromBody), R"({"input":5})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), "5");
}

TEST(InterpreterTest, EmptyBodySeedsJsonNull) {
  Harness harness;
  harness.interpreter_->Start(std::string(kIdentityStep));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), "null");
}

TEST(InterpreterTest, EmptyBodyIsTheDocumentInputSubject) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentInputIdentity));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), "null");
}

TEST(InterpreterTest, OpaqueBodyHasNoJsonSubject) {
  Harness harness;
  harness.interpreter_->Start(std::string(kIdentityStep), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterTest, MarksWorkflowFailedForNonJsonSubjectDuringExecution) {
  Harness harness;
  harness.interpreter_->Start(std::string(kTwoStepsWithExpression));
  test::CompleteSuccess(harness.submitter_, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("two"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterValidationTest, UnknownKeyFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kUnknownKey));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, BadRequirementFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kBadRequirement));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, PartialWrapperFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kPartialWrapper));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, MalformedNestedExpressionFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kNestedMalformed));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, ReservedTargetIsUnimplemented) {
  Harness harness;
  harness.interpreter_->Start(std::string(kReservedTarget));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNIMPLEMENTED);
  EXPECT_NE(harness.results_[0].body().find("recognized"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, UnknownTargetIsMalformedRequest) {
  Harness harness;
  harness.interpreter_->Start(std::string(kUnknownTarget));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("not recognized"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, DeclaredFunctionIsUnimplementedAndDistinct) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDeclaredFunction));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNIMPLEMENTED);
  EXPECT_NE(harness.results_[0].body().find("declared function"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, LooseModeIsRejected) {
  Harness harness;
  harness.interpreter_->Start(std::string(kLooseMode));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("loose"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(ValidateDocumentTest, ValidationFailureCarriesValidationPhase) {
  auto parsed = ::strij::openworkflow::Parse(kUnknownKey);
  ASSERT_TRUE(parsed.has_value());
  const Document document = std::move(*parsed);
  ExpressionEngine engine(test::JqEvaluatorConfig());

  auto failure = ValidateDocument(document, engine);
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->phase_, Phase::kValidation);
}

TEST(ValidateDocumentTest, DataDependentDocumentPassesValidation) {
  auto parsed = ::strij::openworkflow::Parse(kTwoStepsWithExpression);
  ASSERT_TRUE(parsed.has_value());
  const Document document = std::move(*parsed);
  ExpressionEngine engine(test::JqEvaluatorConfig());

  EXPECT_FALSE(ValidateDocument(document, engine).has_value());
}

TEST(InterpreterValidationTest, MalformedCallInsideForkBranchFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kBranchUnknownTarget));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("not recognized"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, CompetingForkWithoutBranchesIsMalformed) {
  Harness harness;
  harness.interpreter_->Start(std::string(kEmptyCompetingFork));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("at least one branch"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(ForkInterpreterTest, SubmitsEveryBranchBeforeResolving) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkThreeBranches));

  ASSERT_EQ(harness.submitter_.size(), 3U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 2, "3");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[1,2,3]");
}

TEST(ForkInterpreterTest, EmptyJoinAllForkJoinsToTheEmptyArray) {
  Harness harness;
  harness.interpreter_->Start(std::string(kEmptyJoinAllFork));

  ASSERT_EQ(harness.submitter_.size(), 0U);
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[]");
  EXPECT_TRUE(harness.results_[0].is_final());
}

TEST(ForkInterpreterTest, JoinAllOrdersResultsByDeclarationNotCompletion) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkThreeBranches));
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 2, "3");
  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  test::CompleteSuccessAt(harness.submitter_, 1, "2");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[1,2,3]");
}

TEST(ForkInterpreterTest, JoinAllArrayIsTheFollowingStepSubject) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkJoinAllThenStep));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"a":1})");
  test::CompleteSuccessAt(harness.submitter_, 1, R"({"b":2})");

  ASSERT_EQ(harness.submitter_.size(), 3U);
  ASSERT_NE(harness.submitter_.at(2), nullptr);
  const auto& parameters = harness.submitter_.at(2)->task_.parameters();
  const auto parameter = parameters.find("p");
  ASSERT_NE(parameter, parameters.end());
  EXPECT_EQ(parameter->second, "2");
}

TEST(ForkInterpreterTest, JoinAllFailsOnAnOpaqueBranchResult) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkThreeBranches));
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");
  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  test::CompleteSuccessAt(harness.submitter_, 2, "3");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("branch 'a'"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not a JSON value"), std::string::npos);
}

TEST(ForkInterpreterTest, NestedDoFailureInsideABranchNamesTheInnerStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkNestedDoFailure));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteErrorAt(harness.submitter_, 0, "boom", task::TASK_STATUS_CAPACITY_REFUSED);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_EQ(harness.results_[0].body(), "branch 'chained': step 'inner': boom");

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  EXPECT_EQ(harness.results_.size(), 1U);
}

TEST(ForkInterpreterTest, NestedJoinFailureIsNamedByTheInnerBranchOnly) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkNestedJoinFailure));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_EQ(harness.results_[0].body(), "branch 'leaf': the branch result is not a JSON value");
}

TEST(ForkInterpreterTest, CompeteResolvesWithTheFirstTerminalResult) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkCompete));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 1, "7");

  ASSERT_EQ(harness.submitter_.size(), 3U);
  ASSERT_NE(harness.submitter_.at(2), nullptr);
  EXPECT_EQ(harness.submitter_.at(2)->task_.body(), "7");
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 2, "9");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "9");
}

TEST(ForkInterpreterTest, LateOutcomeAfterTheCompeteWinnerChangesNothing) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkCompete));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 1, "7");
  EXPECT_TRUE(harness.results_.empty());

  // The losing branch's outcome arrives after the fork resolved. It must not
  // re-enter the workflow or change the join value.
  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  EXPECT_TRUE(harness.results_.empty());
  EXPECT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 2, "9");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "9");
}

TEST(ForkInterpreterTest, JoinAllFailsFastWhenABranchFails) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkThreeBranches));
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteErrorAt(harness.submitter_, 0, "boom", task::TASK_STATUS_CAPACITY_REFUSED);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_NE(harness.results_[0].body().find("branch 'a'"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("boom"), std::string::npos);

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  test::CompleteSuccessAt(harness.submitter_, 2, "3");
  EXPECT_EQ(harness.results_.size(), 1U);
}

TEST(ForkInterpreterTest, CompeteCanBeDecidedByTheFirstFailure) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkCompete));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteErrorAt(harness.submitter_, 0, "boom", task::TASK_STATUS_UNAVAILABLE);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNAVAILABLE);
  EXPECT_NE(harness.results_[0].body().find("branch 'fast'"), std::string::npos);

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  EXPECT_EQ(harness.results_.size(), 1U);
}

TEST(ForkInterpreterTest, InlineBranchCompletionDoesNotDisturbItsSibling) {
  Harness harness;
  auto inline_count = std::make_shared<int>(0);
  harness.submitter_.SetOnSubmit(
      [inline_count](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
        if (++*inline_count != 1) {
          return;
        }

        const std::string body = "1";
        const auto data = std::as_bytes(std::span(body.data(), body.size()));
        receiver.Deliver(data, true);
      });

  harness.interpreter_->Start(std::string(kForkTwoBranches));

  // Branch a completed inline; branch b was still submitted and awaits.
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[1,2]");
}

TEST(ForkInterpreterTest, MultiStepBranchKeepsItsOwnChain) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkMultiStepBranch));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  // The sibling's result must not become the chained branch's subject.
  test::CompleteSuccessAt(harness.submitter_, 1, R"({"value":"other"})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"value":"one"})");
  ASSERT_EQ(harness.submitter_.size(), 3U);
  ASSERT_NE(harness.submitter_.at(2), nullptr);
  EXPECT_EQ(harness.submitter_.at(2)->task_.body(), R"({"value":"one"})");

  const auto& parameters = harness.submitter_.at(2)->task_.parameters();
  const auto parameter = parameters.find("p");
  ASSERT_NE(parameter, parameters.end());
  EXPECT_EQ(parameter->second, "one");

  test::CompleteSuccessAt(harness.submitter_, 2, R"({"value":"two"})");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"([{"value":"two"},{"value":"other"}])");
}

TEST(ForkInterpreterTest, CompeteAbandonsOutstandingGrandchildren) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkCompeteAbandonsNested));

  // x1, y1, and the plain branch are submitted; the nested branch is waiting on
  // the inner fork.
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 2, "1");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "1");

  // The inner branch's first child completes after the outer fork resolved. It
  // must not advance that abandoned branch into its second submission.
  test::CompleteSuccessAt(harness.submitter_, 0, "10");
  EXPECT_EQ(harness.submitter_.size(), 3U);
  EXPECT_EQ(harness.results_.size(), 1U);
}

TEST(ForkInterpreterTest, NestedForkJoinBecomesTheBranchResult) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkNested));

  // The nested compete fork submits x and y before the sibling branch.
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 0, "10");
  EXPECT_TRUE(harness.results_.empty());

  // y lost the inner compete and is abandoned.
  test::CompleteSuccessAt(harness.submitter_, 1, "20");
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 2, "30");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[10,30]");
}

TEST(InterpreterTest, DocumentInputPreservesAJsonString) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentInputString), R"({"name":"world"})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"("world")");
}

TEST(InterpreterTest, OpaqueInputPassesThroughWhenNoTransformationIsDeclared) {
  Harness harness;
  harness.interpreter_->Start(std::string(kPlainStep), "not json");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), "not json");
}

TEST(InterpreterInputFromTest, TransformedInputIsTheCallArgumentSubjectAndBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kCallInputFromWithExpression),
                              R"({"payload":{"id":7},"id":99})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"id":7})");
  EXPECT_EQ(harness.submitter_.last()->task_.parameters().at("p"), "7");
}

TEST(InterpreterInputFromTest, TransformedInputPreservesAJsonString) {
  Harness harness;
  harness.interpreter_->Start(std::string(kCallInputFromString), R"({"name":"world"})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"("world")");
}

TEST(InterpreterInputFromTest, TransformedInputSeedsANestedDo) {
  Harness harness;
  harness.interpreter_->Start(std::string(kNestedDoInputFrom), R"({"payload":{"id":5},"id":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"id":5})");
  EXPECT_EQ(harness.submitter_.last()->task_.parameters().at("p"), "5");

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":6})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), R"({"id":6})");
}

TEST(InterpreterInputFromTest, TransformedInputSeedsEveryForkBranch) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkInputFrom), R"({"payload":{"id":5},"id":1})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(0)->task_.body(), R"({"id":5})");
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), R"({"id":5})");
  EXPECT_EQ(harness.submitter_.at(0)->task_.parameters().at("p"), "5");
  EXPECT_EQ(harness.submitter_.at(1)->task_.parameters().at("p"), "5");
}

TEST(InterpreterConditionTest, TrueConditionRunsTheTask) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionTrue), R"({"enabled":true})");
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterConditionTest, FalseConditionSkipsTheWholePipeline) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionFalseSkip), R"({"enabled":false,"id":5})");

  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"enabled":false,"id":5})");
  EXPECT_EQ(harness.submitter_.last()->task_.parameters().at("p"), "5");
}

TEST(InterpreterConditionTest, FalseConditionSkipsANestedStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionNestedDoSkip), R"({"enabled":false})");

  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"enabled":false})");
}

TEST(InterpreterConditionTest, FalseConditionSkipsAForkBranch) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionForkBranchSkip), R"({"enabled":false})");
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"b":2})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"([{"enabled":false},{"b":2}])");
}

TEST(InterpreterConditionTest, NonBooleanConditionFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionNonBoolean), R"({"count":3})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("boolean"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterConditionTest, ConditionOverOpaqueInputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionOpaqueSubject), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, BareAndMalformedConditionsFailBeforeSubmission) {
  for (std::string_view document : {kBareIf, kMalformedIf}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, BareAndMalformedInputFromFailBeforeSubmission) {
  for (std::string_view document : {kBareInputFrom, kMalformedInputFrom, kNestedBareInputFrom}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, BareAndMalformedOutputAsFailBeforeSubmission) {
  for (std::string_view document : {kBareOutputAs, kMalformedOutputAs}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, BareAndMalformedDocumentOutputFailBeforeSubmission) {
  for (std::string_view document : {kBareDocumentOutput, kMalformedDocumentOutput}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterOutputAsTest, ResumedCallOutputIsShapedOnceForTheFollowingStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kCallOutputAs));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), "7");
}

TEST(InterpreterOutputAsTest, InlineCallOutputIsShapedExactlyOnce) {
  Harness harness;
  auto inline_count = std::make_shared<int>(0);
  harness.submitter_.SetOnSubmit(
      [inline_count](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
        if (++*inline_count != 1) {
          return;
        }

        const std::string body = R"({"id":7})";
        const auto data = std::as_bytes(std::span(body.data(), body.size()));
        receiver.Deliver(data, true);
      });

  harness.interpreter_->Start(std::string(kCallOutputAs));

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), "7");
}

TEST(InterpreterOutputAsTest, NestedSequenceOutputIsShaped) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDoOutputAs));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), "7");
}

TEST(InterpreterOutputAsTest, ForkJoinOutputIsShaped) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkOutputAs));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), "7");
}

TEST(InterpreterOutputAsTest, OutputExpressionBindsInputAndOutput) {
  Harness harness;
  harness.interpreter_->Start(std::string(kOutputAsUsesInputAndOutput), R"({"seed":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"seed":1})");

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"name":"pet"})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(),
            R"({"after":{"name":"pet"},"before":{"seed":1}})");
}

TEST(InterpreterOutputAsTest, OpaqueRawOutputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kOutputAsOpaqueOutput), R"({"id":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterOutputAsTest, OpaqueTransformedInputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kOutputAsOpaqueInput));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 1, R"({"id":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("two"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("input"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterDocumentOutputTest, ShapesTheFinalResultExactlyOnce) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentOutputAs));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"petId":7})");
  EXPECT_TRUE(harness.results_[0].is_final());
}

TEST(InterpreterDocumentOutputTest, OpaqueFinalValueFailsTheRun) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentOutputOpaque));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterContextTest, DocumentInputObservesTheEmptySeed) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentInputObservesEmptyContext), R"({"id":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"context":{}})");
}

TEST(InterpreterContextTest, AWorkflowWithoutExportObservesAnEmptyObject) {
  Harness harness;
  harness.interpreter_->Start(std::string(kNoExportLeavesContextEmpty));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "1");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "{}");
}

TEST(InterpreterContextTest, ConditionsAndCallParametersObserveTheLiveContext) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionAndParameterReadContext));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"ignored":true})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.parameters().at("p"), "abc");
}

TEST(InterpreterExportTest, MergeCombinesTheCurrentContext) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportMerge));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"ignored":true})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  test::CompleteSuccessAt(harness.submitter_, 1, R"({"ignored":true})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"a":1,"b":2})");
}

TEST(InterpreterExportTest, ExportReplacesTheCurrentContext) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportReplace));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"ignored":true})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  test::CompleteSuccessAt(harness.submitter_, 1, R"({"b":2})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"b":2})");
}

TEST(InterpreterExportTest, ExportBindsInputAndShapedOutput) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportUsesInputAndOutput), R"({"seed":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"name":"pet"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"after":{"after":"pet"},"before":{"seed":1}})");
}

TEST(InterpreterExportTest, ATaskWithoutExportLeavesTheContextUnchanged) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportAfterNoExportTask));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"ignored":true})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  test::CompleteSuccessAt(harness.submitter_, 1, R"({"b":2})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"a":1})");
}

TEST(InterpreterExportTest, AnExportWithoutAsLeavesTheContextUnchanged) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExplicitEmptyExport));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"ignored":true})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  test::CompleteSuccessAt(harness.submitter_, 1, R"({"b":2})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"a":1})");
}

TEST(InterpreterExportTest, ASkippedTaskDoesNotExport) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportSkippedTask));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"ignored":true})");

  ASSERT_EQ(harness.submitter_.size(), 1U);
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"a":1})");
}

TEST(InterpreterExportTest, AFailedChildDoesNotExport) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportFailedChild));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteError(harness.submitter_, "boom", task::TASK_STATUS_CAPACITY_REFUSED);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
}

TEST(InterpreterExportTest, ACompositeDoExportsItsShapedOutput) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDoExport));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"context":{"id":7}})");
}

TEST(InterpreterExportTest, AForkExportsItsShapedJoinValue) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkExport));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"joined":7})");
}

TEST(InterpreterExportTest, OpaqueTransformedOutputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportOpaqueOutput), R"({"id":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterExportTest, OpaqueTransformedInputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportOpaqueInput), "not json");
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":1})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("input"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterExportTest, ANonObjectExportResultFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kExportNonObject), R"({"value":7})");
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"value":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("must be an object"), std::string::npos);
}

TEST(InterpreterExportTest, ALocalChildCompletingInsideSubmitExportsExactlyOnce) {
  Harness harness;
  harness.submitter_.SetOnSubmit([](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
    const std::string body = R"("done")";
    const auto data = std::as_bytes(std::span(body.data(), body.size()));
    receiver.Deliver(data, true);
  });

  harness.interpreter_->Start(std::string(kExportInlineExactlyOnce));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"count":1})");
}

TEST(InterpreterValidationTest, BareAndMalformedExportAsFailBeforeSubmission) {
  for (std::string_view document : {kBareExportAs, kMalformedExportAs}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, ExportInsideAForkBranchIsUnimplemented) {
  for (std::string_view document :
       {kExportInsideForkBranch, kExportInsideNestedDoInBranch, kExportInsideNestedForkInBranch}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNIMPLEMENTED);
    EXPECT_NE(harness.results_[0].body().find("fork branch"), std::string::npos);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

// ---------------------------------------------------------------------------
// `set`, `switch`, and flow directives
// ---------------------------------------------------------------------------

constexpr std::string_view kSetObjectMixed = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-object
  version: '0.1.0'
input:
  from: '${ {"configuration":{"color":"red"},"id":7} }'
do:
  - configure:
      set:
        color: '${ .configuration.color }'
        id: '${ .id }'
        label: literal
)YAML";

constexpr std::string_view kSetScalarExpression = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-scalar-expression
  version: '0.1.0'
do:
  - color:
      set: '${ .configuration.color }'
)YAML";

constexpr std::string_view kSetBareScalar = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-bare-scalar
  version: '0.1.0'
do:
  - text:
      set: /bin/task.sh
)YAML";

constexpr std::string_view kSetOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-output
  version: '0.1.0'
do:
  - shaped:
      set: '${ {name: .name} }'
      output:
        as: '${ .name }'
)YAML";

constexpr std::string_view kSetExportAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-export
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - configure:
      set: '${ {token: .token} }'
      export:
        as: '${ {token: $output.token} }'
)YAML";

constexpr std::string_view kSetDoesNotModifyContext = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-no-export
  version: '0.1.0'
output:
  as: '${ $context }'
do:
  - configure:
      set: '${ {token: .token} }'
)YAML";

constexpr std::string_view kSetOpaqueLiteral = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-opaque-literal
  version: '0.1.0'
do:
  - text:
      set: fixed
)YAML";

constexpr std::string_view kSetOpaqueExpression = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-opaque-expression
  version: '0.1.0'
do:
  - expr:
      set: '${ .name }'
)YAML";

constexpr std::string_view kSwitchFirstMatch = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-first
  version: '0.1.0'
do:
  - choose:
      switch:
        - alpha:
            when: '${ .kind == "a" }'
            then: toAlpha
        - beta:
            when: '${ true }'
            then: toBeta
  - toAlpha:
      set: A
      then: end
  - toBeta:
      set: B
      then: end
)YAML";

constexpr std::string_view kSwitchFallback = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-fallback
  version: '0.1.0'
do:
  - choose:
      switch:
        - matched:
            when: '${ .kind == "a" }'
            then: toAlpha
        - fallback:
            then: toFallback
  - toAlpha:
      set: A
      then: end
  - toFallback:
      set: F
      then: end
)YAML";

constexpr std::string_view kSwitchFallbackDeclaredFirst = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-fallback-first
  version: '0.1.0'
do:
  - choose:
      switch:
        - fallback:
            then: toFallback
        - matched:
            when: '${ .kind == "a" }'
            then: toAlpha
  - toAlpha:
      set: A
      then: end
  - toFallback:
      set: F
      then: end
)YAML";

constexpr std::string_view kSwitchOwnThen = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-own-then
  version: '0.1.0'
do:
  - choose:
      switch:
        - missed:
            when: '${ false }'
            then: toAlpha
      then: toOwn
  - toOwn:
      set: O
      then: end
  - toAlpha:
      set: A
      then: end
)YAML";

constexpr std::string_view kSwitchNoMatchContinues = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-no-match
  version: '0.1.0'
do:
  - choose:
      switch:
        - missed:
            when: '${ false }'
            then: toAlpha
  - next:
      set: N
      then: end
  - toAlpha:
      set: A
      then: end
)YAML";

constexpr std::string_view kSwitchNonBoolean = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-non-boolean
  version: '0.1.0'
do:
  - choose:
      switch:
        - flagged:
            when: '${ .value }'
            then: toAlpha
  - toAlpha:
      set: A
      then: end
)YAML";

constexpr std::string_view kSwitchOpaqueConditional = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-opaque
  version: '0.1.0'
do:
  - choose:
      switch:
        - always:
            when: '${ true }'
            then: toAlpha
  - toAlpha:
      set: A
      then: end
)YAML";

constexpr std::string_view kSwitchDefaultOnlyOpaque = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-default-opaque
  version: '0.1.0'
do:
  - choose:
      switch:
        - fallback:
            then: toFallback
  - toFallback:
      set: F
      then: end
)YAML";

constexpr std::string_view kSwitchInputOutputExport = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-pipeline
  version: '0.1.0'
output:
  as: '${ {ctx: $context, data: .} }'
do:
  - choose:
      input:
        from: '${ .payload }'
      output:
        as: '${ {chosen: .kind} }'
      export:
        as: '${ {seen: $output.chosen} }'
      switch:
        - matched:
            when: '${ .kind == "a" }'
            then: continue
)YAML";

constexpr std::string_view kSwitchSkipped = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-skipped
  version: '0.1.0'
do:
  - choose:
      if: '${ false }'
      switch:
        - matched:
            when: '${ true }'
            then: toAlpha
  - next:
      set: N
      then: end
  - toAlpha:
      set: A
      then: end
)YAML";

constexpr std::string_view kSwitchInsideDoBranch = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-do-branch
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - branch:
              do:
                - choose:
                    switch:
                      - matched:
                          when: '${ .kind == "a" }'
                          then: chosen
                - skipped:
                    set: S
                - chosen:
                    set: C
)YAML";

constexpr std::string_view kSwitchDirectBranchEnd = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-direct-branch
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - direct:
              switch:
                - done:
                    when: '${ true }'
                    then: end
          - loser:
              set: L
)YAML";

constexpr std::string_view kSetArray = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-array
  version: '0.1.0'
do:
  - configure:
      set:
        - one
        - two
)YAML";

constexpr std::string_view kRootEndAppliesDocumentOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: root-end
  version: '0.1.0'
output:
  as: '${ {final: .} }'
do:
  - one:
      call: strij
      with:
        type: echo
      then: end
  - two:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kValidForwardBackwardTargets = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: valid-jumps
  version: '0.1.0'
do:
  - a:
      set: '1'
      then: b
  - b:
      set: '2'
      then: a
)YAML";

constexpr std::string_view kUnknownThenTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: unknown-then
  version: '0.1.0'
do:
  - a:
      set: '1'
      then: missing
)YAML";

constexpr std::string_view kCrossScopeThenTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: cross-scope-then
  version: '0.1.0'
do:
  - outer:
      do:
        - inner:
            set: '1'
            then: sibling
  - sibling:
      set: '2'
)YAML";

constexpr std::string_view kDuplicateTaskNames = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: duplicate-names
  version: '0.1.0'
do:
  - dup:
      set: '1'
  - dup:
      set: '2'
)YAML";

constexpr std::string_view kBranchSiblingTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: branch-sibling
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              set: '1'
              then: b
          - b:
              set: '2'
)YAML";

constexpr std::string_view kBranchDoOwnBodyTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: branch-do-body
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - branch:
              do:
                - x:
                    set: '1'
                    then: y
                - y:
                    set: '2'
)YAML";

constexpr std::string_view kSwitchInvalidWhen = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-invalid-when
  version: '0.1.0'
do:
  - choose:
      switch:
        - bad:
            when: '${ .x( }'
            then: toAlpha
  - toAlpha:
      set: A
)YAML";

constexpr std::string_view kSwitchMissingThen = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-missing-then
  version: '0.1.0'
do:
  - choose:
      switch:
        - fallback:
            when: '${ true }'
  - toAlpha:
      set: A
)YAML";

constexpr std::string_view kSwitchTwoDefaults = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: switch-two-defaults
  version: '0.1.0'
do:
  - choose:
      switch:
        - one:
            then: toAlpha
        - two:
            then: toAlpha
  - toAlpha:
      set: A
)YAML";

constexpr std::string_view kSetPartialWrapper = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-partial
  version: '0.1.0'
do:
  - configure:
      set: 'jobs/${ .id }'
)YAML";

constexpr std::string_view kSetInvalidProgram = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set-invalid
  version: '0.1.0'
do:
  - configure:
      set: '${ .x( }'
)YAML";

TEST(InterpreterSetTest, ObjectMembersMixExpressionsAndLiterals) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSetObjectMixed));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"color":"red","id":7,"label":"literal"})");
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterSetTest, ScalarExpressionReplacesTheDataChain) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSetScalarExpression),
                              R"({"configuration":{"color":"blue"}})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("blue")");
}

TEST(InterpreterSetTest, BareScalarIsLiteralText) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSetBareScalar));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("/bin/task.sh")");
}

TEST(InterpreterSetTest, OutputAsShapesTheSetResult) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSetOutputAs), R"({"name":"world"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("world")");
}

TEST(InterpreterSetTest, ExportAsWritesTheContextButTheSetDoesNot) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSetExportAs), R"({"token":"abc"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"token":"abc"})");
}

TEST(InterpreterSetTest, ASetWithoutExportLeavesTheContextUnchanged) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSetDoesNotModifyContext), R"({"token":"abc"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "{}");
}

TEST(InterpreterSetTest, LiteralOnlySetWorksOverOpaqueInput) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSetOpaqueLiteral), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("fixed")");
}

TEST(InterpreterSetTest, ExpressionOverOpaqueInputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSetOpaqueExpression), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("expr"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterFlowTest, EndAtRootStillAppliesTheDocumentOutput) {
  Harness harness;
  harness.interpreter_->Start(std::string(kRootEndAppliesDocumentOutput));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "1");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"final":1})");
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterSwitchTest, TheFirstMatchingCaseWins) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchFirstMatch), R"({"kind":"a"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("A")");
}

TEST(InterpreterSwitchTest, TheFallbackHandlesAnUnmatchedInput) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchFallback), R"({"kind":"z"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("F")");
}

TEST(InterpreterSwitchTest, ADeclaredFirstFallbackWaitsForConditionalCases) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchFallbackDeclaredFirst), R"({"kind":"a"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("A")");
}

TEST(InterpreterSwitchTest, TheSwitchOwnThenHandlesNoMatch) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchOwnThen), R"({"kind":"z"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("O")");
}

TEST(InterpreterSwitchTest, NoMatchAndNoThenContinues) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchNoMatchContinues), R"({"kind":"z"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("N")");
}

TEST(InterpreterSwitchTest, ANonBooleanWhenFailsTheStepNamingTheCase) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchNonBoolean), R"({"value":3})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("choose"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("flagged"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("boolean"), std::string::npos);
}

TEST(InterpreterSwitchTest, AConditionalCaseOverOpaqueInputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchOpaqueConditional), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("choose"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterSwitchTest, ADefaultOnlySwitchNeedsNoJsonSubject) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchDefaultOnlyOpaque), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("F")");
}

TEST(InterpreterSwitchTest, InputFromOutputAsAndExportAsApplyAroundTheSwitch) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchInputOutputExport), R"({"payload":{"kind":"a"}})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"ctx":{"seen":"a"},"data":{"chosen":"a"}})");
}

TEST(InterpreterSwitchTest, AFalseIfSkipsTheSwitchEntirely) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchSkipped), R"({"kind":"a"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"("N")");
}

TEST(InterpreterSwitchTest, ASwitchInsideADoBranchSelectsSiblingWork) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchInsideDoBranch), R"({"kind":"a"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"(["C"])");
}

TEST(InterpreterSwitchTest, ADirectSwitchBranchCanConcludeTheWorkflow) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSwitchDirectBranchEnd), R"({"kind":"a"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"kind":"a"})");
}

TEST(ValidateDocumentTest, ScopeAwareFlowDirectivesAcceptValidTargets) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  for (std::string_view document : {kValidForwardBackwardTargets, kBranchDoOwnBodyTarget}) {
    auto parsed = ::strij::openworkflow::Parse(document);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(ValidateDocument(*parsed, engine).has_value());
  }
}

TEST(InterpreterValidationTest, MalformedFlowDirectivesFailBeforeAnySubmission) {
  for (std::string_view document :
       {kUnknownThenTarget, kCrossScopeThenTarget, kDuplicateTaskNames, kBranchSiblingTarget}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, MalformedSwitchCasesFailBeforeAnySubmission) {
  for (std::string_view document : {kSwitchInvalidWhen, kSwitchMissingThen, kSwitchTwoDefaults}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, MalformedSetDataFailsBeforeAnySubmission) {
  for (std::string_view document : {kSetPartialWrapper, kSetInvalidProgram, kSetArray}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

// ---------------------------------------------------------------------------
// `for`
// ---------------------------------------------------------------------------

constexpr std::string_view kForAccumulate = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-accumulate
  version: '0.1.0'
input:
  from: '${ {"colors":["red","green","blue"]} }'
do:
  - loop:
      for:
        in: '${ .colors }'
      do:
        - append:
            set: '${ {items: ((.items // []) + [$item]), indexes: ((.indexes // []) + [$index])} }'
)YAML";

constexpr std::string_view kForCustomVariables = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-custom
  version: '0.1.0'
input:
  from: '${ {"values":["red","green"]} }'
do:
  - loop:
      for:
        each: $color
        at: position
        in: '${ .values }'
      do:
        - append:
            set: '${ {colors: ((.colors // []) + [$color]), positions: ((.positions // []) + [$position])} }'
)YAML";

constexpr std::string_view kForInlineArray = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-inline
  version: '0.1.0'
input:
  from: '${ {"suffix":"green"} }'
do:
  - loop:
      for:
        in:
          - red
          - '${ .suffix }'
          - 7
      do:
        - append:
            set: '${ {items: ((.items // []) + [$item])} }'
)YAML";

constexpr std::string_view kForEmptyOrNull = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-empty
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .items }'
      do:
        - append:
            set: '${ {items: ((.items // []) + [$item])} }'
)YAML";

constexpr std::string_view kForNonArray = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-non-array
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .items }'
      do:
        - append:
            set: '${ {items: ($item)} }'
)YAML";

constexpr std::string_view kForOpaqueSubject = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-opaque
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .items }'
      do:
        - append:
            set: '${ {items: ($item)} }'
)YAML";

constexpr std::string_view kForWhile = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-while
  version: '0.1.0'
input:
  from: '${ [10,20,30] }'
do:
  - loop:
      for:
        in: '${ . }'
      while: '${ $index < 2 and .value < 20 }'
      do:
        - body:
            set: '${ {value: $item} }'
)YAML";

constexpr std::string_view kForWhileNonBoolean = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-while-non-boolean
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ [1,2] }'
      while: '${ .value }'
      do:
        - body:
            set: '${ {value: $item} }'
)YAML";

constexpr std::string_view kForWhileOpaque = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-while-opaque
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ [1,2] }'
      while: '${ . }'
      do:
        - body:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kForSwitchSetExpressions = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-slots
  version: '0.1.0'
input:
  from: '${ {"colors":["a","b"]} }'
do:
  - loop:
      for:
        each: color
        at: index
        in: '${ .colors }'
      do:
        - choose:
            if: '${ $index >= 0 }'
            input:
              from: '${ . + {item: $color, index: $index} }'
            switch:
              - matched:
                  when: '${ .item == $color and .index == $index }'
                  then: record
        - record:
            set: '${ {items: ((.items // []) + [$color]), indexes: ((.indexes // []) + [$index])} }'
)YAML";

constexpr std::string_view kForCallSuspended = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-call
  version: '0.1.0'
input:
  from: '${ {"colors":["red","green"]} }'
output:
  as: '${ {context: $context, result: .} }'
do:
  - loop:
      for:
        each: color
        at: index
        in: '${ .colors }'
      do:
        - body:
            call: strij
            input:
              from: '${ . + {color: $color, index: $index} }'
            output:
              as: '${ {color: $color, index: $index, echo: .echo} }'
            export:
              as: '${ {lastColor: $color} }'
            with:
              type: echo
              parameters:
                p: '${ $index }'
)YAML";

constexpr std::string_view kForNestedShadowing = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-nested
  version: '0.1.0'
input:
  from: '${ {"outer":["red"],"inner":["a","b"]} }'
do:
  - outer:
      for:
        each: color
        at: outerPosition
        in: '${ .outer }'
      do:
        - inner:
            for:
              each: color
              at: position
              in: '${ .inner }'
            do:
              - record:
                  set: '${ {seen: ((.seen // []) + [{color: $color, position: $position, outer: $outerPosition}])} }'
)YAML";

constexpr std::string_view kForReference = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-reference
  version: '0.1.0'
input:
  from: '${ {"values":["red","green","blue"]} }'
do:
  - loop:
      for:
        each: color
        at: index
        in: '${ .values }'
      output:
        as: '${ {processed: {colors: .colors, indexes: .indexes}} }'
      do:
        - append:
            set: '${ {colors: ((.colors // []) + [$color]), indexes: ((.indexes // []) + [$index])} }'
)YAML";

constexpr std::string_view kForBodyFailure = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-body-failure
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ [1,2] }'
      do:
        - body:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kForBound = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-bound
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ [range(0; 10)] }'
      do: []
)YAML";

constexpr std::string_view kForSuspending = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-suspending
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ [1,2,3,4,5] }'
      do:
        - body:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kBareForIn = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-for-in
  version: '0.1.0'
do:
  - loop:
      for:
        in: .colors
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kMalformedForIn = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-for-in
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .colors( }'
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kPartialForIn = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: partial-for-in
  version: '0.1.0'
do:
  - loop:
      for:
        in: 'colors/${ .colors }'
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kBareForWhile = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-for-while
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .colors }'
      while: .ok
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kMalformedForWhile = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-for-while
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .colors }'
      while: '${ .ok( }'
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kForInlineNotArray = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-inline-object
  version: '0.1.0'
do:
  - loop:
      for:
        in:
          a: b
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kForInvalidName = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-invalid-name
  version: '0.1.0'
do:
  - loop:
      for:
        each: 9bad
        in: '${ .colors }'
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kForDuplicateName = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-duplicate-name
  version: '0.1.0'
do:
  - loop:
      for:
        each: color
        at: $color
        in: '${ .colors }'
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kForReservedName = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-reserved-name
  version: '0.1.0'
do:
  - loop:
      for:
        each: context
        in: '${ .colors }'
      do:
        - body:
            set: '${ . }'
)YAML";

constexpr std::string_view kForMalformedBody = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-malformed-body
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .colors }'
      do:
        - body:
            call: bogus
            with:
              type: echo
)YAML";

constexpr std::string_view kForBodyReadsItem = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-body-item
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .colors }'
      do:
        - body:
            call: strij
            with:
              type: echo
              parameters:
                p: '${ $item }'
)YAML";

constexpr std::string_view kForBodyExportInFork = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-body-export
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - branch:
              for:
                in: '${ .colors }'
              do:
                - body:
                    call: strij
                    export:
                      as: '${ {a: 1} }'
                    with:
                      type: echo
)YAML";

constexpr std::string_view kForExitInBody = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-exit
  version: '0.1.0'
input:
  from: '${ [1,2] }'
do:
  - loop:
      for:
        in: '${ . }'
      do:
        - first:
            call: strij
            with:
              type: echo
            then: exit
        - skipped:
            set: '${ {bad: true} }'
  - after:
      set: '${ . }'
)YAML";

constexpr std::string_view kForForkBranch = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-branch
  version: '0.1.0'
input:
  from: '${ [] }'
do:
  - fan:
      fork:
        branches:
          - a:
              for:
                in: '${ [1,2] }'
              do:
                - step:
                    set: '${ . + [$item] }'
)YAML";

constexpr std::string_view kForTaskExport = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-export
  version: '0.1.0'
input:
  from: '${ 0 }'
do:
  - loop:
      for:
        in: '${ [1,2] }'
      output:
        as: '${ {sum: .} }'
      export:
        as: '${ {total: .sum} }'
      do:
        - step:
            set: '${ . + $item }'
  - after:
      set: '${ $context }'
)YAML";

constexpr std::string_view kForInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-input
  version: '0.1.0'
do:
  - loop:
      input:
        from: '${ {"colors":["red","green"]} }'
      for:
        in: '${ .colors }'
      do:
        - step:
            set: '${ {colors: .colors, seen: ((.seen // []) + [$item])} }'
)YAML";

constexpr std::string_view kForThenFollowingStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-position
  version: '0.1.0'
input:
  from: '${ 0 }'
do:
  - loop:
      for:
        in: '${ [1,2,3] }'
      do:
        - step:
            set: '${ . + 1 }'
  - after:
      set: '${ {done: true, value: .} }'
)YAML";

TEST(InterpreterForTest, IteratesInOrderWithDefaultLoopVariablesAndAccumulator) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForAccumulate));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"indexes":[0,1,2],"items":["red","green","blue"]})");
  EXPECT_TRUE(harness.results_[0].is_final());
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterForTest, CustomLoopVariableNamesAreBound) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForCustomVariables));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"colors":["red","green"],"positions":[0,1]})");
}

TEST(InterpreterForTest, AnInlineArrayEvaluatesWrappedMembersAndKeepsLiterals) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForInlineArray));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"items":["red","green",7]})");
}

TEST(InterpreterForTest, EmptyAndNullCollectionsPreserveTheTransformedInput) {
  for (std::string_view body : {R"({"items":[]})", R"({"items":null})"}) {
    Harness harness;
    harness.interpreter_->Start(std::string(kForEmptyOrNull), body);

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].body(), body);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterForTest, ANonArrayCollectionFailsNamingTheTask) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForNonArray), R"({"items":5})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("loop"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("array"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterForTest, AnExpressionCollectionOverOpaqueInputFails) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForOpaqueSubject), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("loop"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterForTest, WhileReadsTheAccumulatorAndCurrentLoopVariables) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForWhile));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"value":20})");
}

TEST(InterpreterForTest, ANonBooleanWhileFailsNamingTheTask) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForWhileNonBoolean));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("loop"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("boolean"), std::string::npos);
}

TEST(InterpreterForTest, WhileOverAnOpaqueAccumulatorFailsNamingTheTask) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForWhileOpaque));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("loop"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterForTest, BodyIfInputSwitchAndSetSeeTheLoopVariables) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForSwitchSetExpressions));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"indexes":[0,1],"items":["a","b"]})");
}

TEST(InterpreterForTest, ASuspendedCallKeepsItsLoopVariablesAcrossTheResult) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForCallSuspended));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.at(0)->task_.body(),
            R"({"color":"red","colors":["red","green"],"index":0})");
  EXPECT_EQ(harness.submitter_.at(0)->task_.parameters().at("p"), "0");

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"echo":"one"})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), R"({"color":"green","echo":"one","index":1})");
  EXPECT_EQ(harness.submitter_.at(1)->task_.parameters().at("p"), "1");

  test::CompleteSuccessAt(harness.submitter_, 1, R"({"echo":"two"})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(
      harness.results_[0].body(),
      R"({"context":{"lastColor":"green"},"result":{"color":"green","echo":"two","index":1}})");
}

TEST(InterpreterForTest, NestedLoopsInheritOuterVariablesAndShadowByName) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForNestedShadowing));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(
      harness.results_[0].body(),
      R"({"seen":[{"color":"a","outer":0,"position":0},{"color":"b","outer":0,"position":1}]})");
}

TEST(InterpreterForTest, BodyFailurePropagatesItsExistingOrigin) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForBodyFailure));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteError(harness.submitter_, "boom", task::TASK_STATUS_CAPACITY_REFUSED);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_EQ(harness.results_[0].body(), "step 'body': boom");
}

TEST(InterpreterForTest, ANonSuspendingLoopTripsTheIterationBudget) {
  Harness harness(3);
  harness.interpreter_->Start(std::string(kForBound));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("loop"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("budget"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterForTest, ALoopThatSuspendsOnEveryIterationDoesNotTripTheBudget) {
  Harness harness(2);
  harness.interpreter_->Start(std::string(kForSuspending));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  for (std::size_t index = 0; index < 5; ++index) {
    test::CompleteSuccessAt(harness.submitter_, index, std::to_string(index));
    if (index + 1 < 5) {
      ASSERT_EQ(harness.submitter_.size(), index + 2);
    }
  }

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "4");
}

TEST(InterpreterForTest, ReferenceColorsAndIndexesScenarioAccumulatesExactly) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForReference));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(),
            R"({"processed":{"colors":["red","green","blue"],"indexes":[0,1,2]}})");
}

TEST(InterpreterForTest, ExitInsideTheBodyCompletesOnlyTheCurrentIteration) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForExitInBody));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 1, "2");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "2");
  EXPECT_EQ(harness.submitter_.size(), 2U);
}

TEST(InterpreterForTest, AForBranchContributesItsAccumulatorToTheJoin) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForForkBranch));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"([[1,2]])");
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterForTest, TheForTaskExportUpdatesTheContextAfterIteration) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForTaskExport));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"total":3})");
}

TEST(InterpreterForTest, TransformedInputSeedsTheLoopAndItsBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForInputFrom));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"colors":["red","green"],"seen":["red","green"]})");
}

TEST(InterpreterForTest, TheStepFollowingTheLoopRunsAfterEveryIteration) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForThenFollowingStep));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"done":true,"value":3})");
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, BareAndMalformedForExpressionsFailBeforeSubmission) {
  for (std::string_view document :
       {kBareForIn, kMalformedForIn, kPartialForIn, kBareForWhile, kMalformedForWhile}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, NonArrayInlineForCollectionAndInvalidNamesFailBeforeSubmission) {
  for (std::string_view document :
       {kForInlineNotArray, kForInvalidName, kForDuplicateName, kForReservedName}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, AMalformedTaskInsideAForBodyFailsBeforeSubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForMalformedBody));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("not recognized"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, AForBodyExpressionMayReferenceItsLoopVariables) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForBodyReadsItem), R"({"colors":["red"]})");

  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.parameters().at("p"), "red");
}

TEST(InterpreterValidationTest, AForBodyInsideAForkBranchInheritsTheExportRejection) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForBodyExportInFork));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNIMPLEMENTED);
  EXPECT_NE(harness.results_[0].body().find("fork branch"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
