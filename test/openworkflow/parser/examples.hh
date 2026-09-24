// Generated from the Open Workflow Specification example corpus.
// Each constant is the verbatim text of one reference example document.
#pragma once

#include <string_view>

namespace strij::openworkflow::test {

inline constexpr std::string_view kDoSingle = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: call-http-shorthand-endpoint
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint: https://petstore.swagger.io/v2/pet/{petId}
)YAML";

inline constexpr std::string_view kDoMultiple = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: call-http-shorthand-endpoint
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint: https://petstore.swagger.io/v2/pet/{petId}
  - buyPet:
      call: http
      with:
        method: put
        endpoint: https://petstore.swagger.io/v2/pet/{petId}
        body: '${ . + { status: "sold" } }'
)YAML";

inline constexpr std::string_view kFor = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-example
  version: '0.1.0'
do:
  - checkup:
      for:
        each: pet
        in: .pets
        at: index
      while: .vet != null
      do:
        - waitForCheckup:
            listen:
              to:
                one:
                  with:
                    type: com.fake.petclinic.pets.checkup.completed.v2
            output:
              as: '.pets + [{ "id": $pet.id }]'        )YAML";

inline constexpr std::string_view kForInlineArray = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-inline-array-example
  version: '0.1.0'
do:
  - logColors:
      for:
        each: color
        in:
          - name: red
            hex: '#FF0000'
          - name: green
            hex: '#00FF00'
          - name: blue
            hex: '#0000FF'
      do:
        - setProcessed:
            set:
              processed: '${ .processed + [$color] }'
)YAML";

inline constexpr std::string_view kForInlinePrimitiveArray = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-inline-primitive-array-example
  version: '0.1.0'
do:
  - sumNumbers:
      for:
        each: number
        in:
          - 1
          - 2
          - 3
      do:
        - add:
            set:
              sum: '${ .sum + $number }'
)YAML";

inline constexpr std::string_view kFork = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-example
  version: '0.1.0'
do:
  - raiseAlarm:
      fork:
        compete: true
        branches:
          - callNurse:
              call: http
              with:
                method: put
                endpoint: https://fake-hospital.com/api/v3/alert/nurses
                body:
                  patientId: ${ .patient.fullName }
                  room: ${ .room.number }
          - callDoctor:
              call: http
              with:
                method: put
                endpoint: https://fake-hospital.com/api/v3/alert/doctor
                body:
                  patientId: ${ .patient.fullName }
                  room: ${ .room.number })YAML";

inline constexpr std::string_view kTryCatch = R"YAML(document:
  dsl: '1.0.3'
  namespace: default
  name: try-catch
  version: '0.1.0'
do:
  - tryGetPet:
      try:
        - getPet:
            call: http
            with:
              method: get
              endpoint: https://petstore.swagger.io/v2/pet/{petId}
      catch:
        errors:
          with:
            type: https://open-workflow-specification.org/spec/1.0.0/errors/communication
            status: 404)YAML";

inline constexpr std::string_view kTryCatchRetryInline = R"YAML(document:
  dsl: '1.0.3'
  namespace: default
  name: try-catch-retry
  version: '0.1.0'
do:
  - tryGetPet:
      try:
        - getPet:
            call: http
            with:
              method: get
              endpoint: https://petstore.swagger.io/v2/pet/{petId}
      catch:
        errors:
          with:
            type: https://open-workflow-specification.org/spec/1.0.0/errors/communication
            status: 503
        retry:
          delay:
            seconds: 3
          backoff:
            exponential: {}
          limit:
            attempt:
              count: 5)YAML";

inline constexpr std::string_view kTryCatchThen = R"YAML(document:
  dsl: '1.0.3'
  namespace: default
  name: try-catch
  version: '0.1.0'
do:
  - tryGetPet:
      try:
        - getPet:
            call: http
            with:
              method: get
              endpoint: https://petstore.swagger.io/v2/pet/{petId}
      catch:
        errors:
          with:
            type: https://open-workflow-specification.org/spec/1.0.0/errors/communication
            status: 404
        as: error
        do:
          - notifySupport:
              emit:
                event:
                  with:
                    source: https://petstore.swagger.io
                    type: io.swagger.petstore.events.pets.not-found.v1
                    data: ${ $error }
          - setError:
              set:
                error: $error
              export:
                as: '$context + { error: $error }'
  - buyPet:
      if: $context.error == null
      call: http
      with:
        method: put
        endpoint: https://petstore.swagger.io/v2/pet/{petId}
        body: '${ . + { status: "sold" } }')YAML";

inline constexpr std::string_view kTryCatchThenDirective = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: try-catch-then-directive
  version: '0.1.0'
do:
  - trySomething:
      try:
        - callExternalService:
            call: http
            with:
              method: get
              endpoint: https://external-service.example.com/api
      catch:
        errors:
          with:
            type: https://serverlessworkflow.io/dsl/errors/types/communication
            status: 503
        then: recordFailure
  - continueProcessing:
      set:
        status: ok
      then: end
  - recordFailure:
      set:
        status: failed
)YAML";

inline constexpr std::string_view kSwitchThenString = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: sample-workflow
  version: 0.1.0
do:
  - processOrder:
      switch:
        - case1:
            when: .orderType == "electronic"
            then: processElectronicOrder
        - case2:
            when: .orderType == "physical"
            then: processPhysicalOrder
        - default:
            then: handleUnknownOrderType
  - processElectronicOrder:
      set:
        validate: true
        status: fulfilled
      then: exit
  - processPhysicalOrder:
      set:
        inventory: clear
        items: 1
        address: Elmer St
      then: exit
  - handleUnknownOrderType:
      set:
        log: warn
        message: something's wrong)YAML";

inline constexpr std::string_view kSet = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set
  version: '0.1.0'
schedule:
  on:
    one:
      with:
        type: io.serverlessworkflow.samples.events.trigger.v1
do:
  - initialize:
      set:
        startEvent: ${ $workflow.input[0] })YAML";

inline constexpr std::string_view kSetExpression = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: set
  version: '0.1.0'
schedule:
  on:
    one:
      with:
        type: io.serverlessworkflow.samples.events.trigger.v1
do:
  - initialize:
      set: ${ $workflow.input[0] })YAML";

inline constexpr std::string_view kWaitInline = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: wait-duration-inline
  version: '0.1.0'
do: 
  - wait30Seconds:
      wait:
        seconds: 30)YAML";

inline constexpr std::string_view kWaitIso8601 = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: wait-duration-8601
  version: '0.1.0'
do: 
  - wait30Seconds:
      wait: PT30S)YAML";

inline constexpr std::string_view kEmit = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: emit
  version: '0.1.0'
do:
  - emitEvent:
      emit:
        event:
          with:
            source: https://petstore.com
            type: com.petstore.order.placed.v1
            data:
              client:
                firstName: Cruella
                lastName: de Vil
              items:
                - breed: dalmatian
                  quantity: 101)YAML";

inline constexpr std::string_view kRaiseInline = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: raise-not-implemented
  version: '0.1.0'
do: 
  - notImplemented:
      raise:
        error:
          type: https://serverlessworkflow.io/errors/not-implemented
          status: 500
          title: Not Implemented
          detail: ${ "The workflow '\( $workflow.definition.document.name ):\( $workflow.definition.document.version )' is a work in progress and cannot be run yet" })YAML";

inline constexpr std::string_view kRaiseReusable = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: raise-not-implemented
  version: '0.1.0'
use:
  errors:
    notImplemented:
      type: https://serverlessworkflow.io/errors/not-implemented
      status: 500
      title: Not Implemented
      detail: ${ "The workflow '\( $workflow.definition.document.name ):\( $workflow.definition.document.version )' is a work in progress and cannot be run yet" }
do: 
  - notImplemented:
      raise:
        error: notImplemented)YAML";

inline constexpr std::string_view kRunContainer = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world)YAML";

inline constexpr std::string_view kRunContainerCleanupAlways = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world
          lifetime:
            cleanup: always)YAML";

inline constexpr std::string_view kRunContainerCleanupEventually = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world
          lifetime:
            cleanup: eventually
            after:
              minutes: 30)YAML";

inline constexpr std::string_view kRunContainerStdinArguments = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container-stdin-and-arguments
  version: '0.1.0'
do:
  - setInput:
      set:
        message: Hello World
  - runContainer:
      input:
        from: ${ .message }
      run:
        container:
          image: alpine
          command: |
            input=$(cat)
            echo "STDIN was: $input"
            echo "ARGS are $1 $2"
          stdin: ${ . }
          arguments:
          - Foo
          - Bar
)YAML";

inline constexpr std::string_view kRunContainerWithName = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container-with-name
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world
          name: ${ "hello-\(.workflow.document.name)-\(.task.name)-\(.workflow.id)" })YAML";

inline constexpr std::string_view kRunContainerPullPolicy = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container-with-pull-policy
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world
          pullPolicy: always)YAML";

inline constexpr std::string_view kRunReturnAll = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world
        return: all)YAML";

inline constexpr std::string_view kRunReturnCode = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world
        return: code)YAML";

inline constexpr std::string_view kRunReturnNone = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world
        return: none)YAML";

inline constexpr std::string_view kRunReturnStderr = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-container
  version: '0.1.0'
do:
  - runContainer:
      run:
        container:
          image: hello-world
        return: stderr)YAML";

inline constexpr std::string_view kRunScript = R"YAML(document:
  dsl: 1.0.3
  namespace: examples
  name: run-script-with-stdin-and-arguments
  version: 1.0.0
do:
  - runScript:
      run:
        script:
          language: js
          stdin: "Hello Workflow"
          environment:
           foo: bar
          arguments:
          - hello
          code: |
            // Reading Input from STDIN
            import { readFileSync } from 'node:fs';
            const stdin = readFileSync(process.stdin.fd, 'utf8');
            console.log('stdin > ', stdin) // Output: stdin > Hello Workflow

            // Reading from argv
            const [_, __, arg] = process.argv;
            console.log('arg > ', arg) // Output: arg > hello

            // Reading from env
            const foo = process.env.foo;
            console.log('env > ', foo) // Output: env > bar
)YAML";

inline constexpr std::string_view kRunShell = R"YAML(document:
  dsl: 1.0.3
  namespace: examples
  name: run-shell-with-stdin-and-arguments
  version: 1.0.0
do:
  - setInput:
      set:
        message: Hello World
  - runShell:
      input:
        from: ${ .message }
      run:
        shell:
          stdin: ${ . }
          command: |
            input=$(cat)
            echo "STDIN was: $input"
            echo "ARGS are $1 $2"
          arguments:
          - Foo
          - Bar
)YAML";

inline constexpr std::string_view kRunSubflow = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: run-subflow
  version: '0.1.0'
do:
  - registerCustomer:
      run:
        workflow:
          namespace: test
          name: register-customer
          version: '0.1.0'
          input:
            customer: .user)YAML";

inline constexpr std::string_view kCallHttpInterpolation = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: call-http-shorthand-endpoint
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        headers:
          content-type: application/json
        method: get
        endpoint: ${ "https://petstore.swagger.io/v2/pet/\(.petId)" })YAML";

inline constexpr std::string_view kCallHttpInterpolationShorthand = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: call-http-shorthand-endpoint
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint: https://petstore.swagger.io/v2/pet/{petId}
)YAML";

inline constexpr std::string_view kCallHttpQueryHeaders = R"YAML(# yaml-language-server: $schema=../schema/workflow.yaml
document:
  dsl: '1.0.3'
  namespace: examples
  name: http-query-headers-expressions
  version: '1.0.0'
input:
  schema:
    format: json
    document:
      type: object
      required:
        - searchQuery
      properties:
        searchQuery:
          type: string
do:
  - setQueryAndHeaders:
      set:
        query:
          search: ${.searchQuery}
        headers:
          Accept: application/json
  - searchStarWarsCharacters:
      call: http
      with:
        method: get
        endpoint: https://swapi.dev/api/people/
        headers: ${.headers}
        query: ${.query}
      
)YAML";

inline constexpr std::string_view kCallHttpQueryParameters = R"YAML(# yaml-language-server: $schema=../schema/workflow.yaml
document:
  dsl: '1.0.3'
  namespace: examples
  name: http-query-params
  version: '1.0.0'
input:
  schema:
    format: json
    document:
      type: object
      required:
        - searchQuery
      properties:
        searchQuery:
          type: string
do:
  - searchStarWarsCharacters:
      call: http
      with:
        method: get
        endpoint: https://swapi.dev/api/people/
        query:
          search: ${.searchQuery}
      
)YAML";

inline constexpr std::string_view kCallHttpRedirect = R"YAML(# yaml-language-server: $schema=../schema/workflow.yaml
document:
  dsl: '1.0.3'
  namespace: examples
  name: http-query-params
  version: '1.0.0'
input:
  schema:
    format: json
    document:
      type: object
      required:
        - searchQuery
      properties:
        searchQuery:
          type: string
do:
  - searchStarWarsCharacters:
      call: http
      with:
        method: get
        endpoint: https://swapi.dev/api/people/
        query:
          search: ${.searchQuery}
        redirect: true
      
)YAML";

inline constexpr std::string_view kCallGrpc = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: grpc-example
  version: '0.1.0'
do:
  - greet:
      call: grpc
      with:
        proto: 
          endpoint: file://app/greet.proto
        service:
          name: GreeterApi.Greeter
          host: localhost
          port: 5011
        method: SayHello
        arguments:
          name: ${ .user.preferredDisplayName })YAML";

inline constexpr std::string_view kCallOpenapi = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: openapi-example
  version: '0.1.0'
do:
  - findPet:
      call: openapi
      with:
        document: 
          endpoint: https://petstore.swagger.io/v2/swagger.json
        operationId: findPetsByStatus
        parameters:
          status: available)YAML";

inline constexpr std::string_view kCallOpenapiRedirect = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: openapi-example
  version: '0.1.0'
do:
  - findPet:
      call: openapi
      with:
        document: 
          endpoint: https://petstore.swagger.io/v2/swagger.json
        operationId: findPetsByStatus
        parameters:
          status: available
        redirect: true)YAML";

inline constexpr std::string_view kCallCustomFunctionInline = R"YAML(document:
  dsl: '1.0.3'
  namespace: samples
  name: call-custom-function-inline
  version: '0.1.0'
use:
  functions:
    getPetById:
      input:
        schema:
          document:
            type: object
            properties:
              petId:
                type: integer
            required: [ petId ]
      call: http
      with:
        method: get
        endpoint: https://petstore.swagger.io/v2/pet/{petId}
do:
  - getPet:
      call: getPetById
      with:
        petId: 69)YAML";

inline constexpr std::string_view kCallCustomFunctionCataloged = R"YAML(document:
  dsl: '1.0.3'
  namespace: samples
  name: call-custom-function-cataloged
  version: '0.1.0'
do:
  - log:
      call: https://raw.githubusercontent.com/serverlessworkflow/catalog/main/functions/log/1.0.0/function.yaml
      with:
        message: Hello, world!
        level: information
        timestamp: true
        format: '{TIMESTAMP} [{LEVEL}] ({CONTEXT}): {MESSAGE}')YAML";

inline constexpr std::string_view kCallAsyncapiPublish = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: bearer-auth
  version: '0.1.0'
do:
  - findPet:
      call: asyncapi
      with:
        document:
          endpoint: https://fake.com/docs/asyncapi.json
        operation: findPetsByStatus
        server:
          name: staging
        message:
          payload:
            petId: ${ .pet.id }
        authentication:
          bearer:
            token: ${ .token }
)YAML";

inline constexpr std::string_view kCallAsyncapiConsumeAmount = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: bearer-auth
  version: '0.1.0'
do:
  - getNotifications:
      call: asyncapi
      with:
        document:
          endpoint: https://fake.com/docs/asyncapi.json
        operation: getNotifications
        protocol: ws
        subscription:
          filter: '${ .correlationId == $context.userId and .payload.from.firstName == $context.contact.firstName and .payload.from.lastName == $context.contact.lastName }'
          consume:
            amount: 5
)YAML";

inline constexpr std::string_view kCallAsyncapiConsumeUntil = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: bearer-auth
  version: '0.1.0'
do:
  - getNotifications:
      call: asyncapi
      with:
        document:
          endpoint: https://fake.com/docs/asyncapi.json
        channel: /notifications
        subscription:
          filter: '${ .correlationId == $context.userId and .payload.from.firstName == $context.contact.firstName and .payload.from.lastName == $context.contact.lastName }'
          consume:
            for:
              minutes: 30
            until: '${ ($context.consumedMessages | length) == 5 }'
)YAML";

inline constexpr std::string_view kCallAsyncapiConsumeWhile = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: bearer-auth
  version: '0.1.0'
do:
  - getNotifications:
      call: asyncapi
      with:
        document:
          endpoint: https://fake.com/docs/asyncapi.json
        operation: getNotifications
        subscription:
          filter: '${ .correlationId == $context.userId and .payload.from.firstName == $context.contact.firstName and .payload.from.lastName == $context.contact.lastName }'
          consume:
            while: '${ ($context.consumedMessages | length) < 5 }'
)YAML";

inline constexpr std::string_view kCallAsyncapiConsumeForever = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: bearer-auth
  version: '0.1.0'
do:
  - getNotifications:
      call: asyncapi
      with:
        document:
          endpoint: https://fake.com/docs/asyncapi.json
        operation: getNotifications
        subscription:
          filter: '${ .correlationId == $context.userId and .payload.from.firstName == $context.contact.firstName and .payload.from.lastName == $context.contact.lastName }'
          consume:
            while: '${ true }'
          foreach:
            item: message
            do:
              - publishCloudEvent:
                  emit:
                    event:
                      with:
                        source: https://open-workflow-specification.org/samples
                        type: io.serverlessworkflow.samples.asyncapi.message.consumed.v1
                        data:
                          message: '${ $message }'
                        
)YAML";

inline constexpr std::string_view kCallMcp = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: mcp-example
  version: '0.1.0'
do:
  - publishMessageToSlack:
      call: mcp
      with:
        method: tools/call
        parameters:
          name: conversations_add_message
          arguments:
            channel_id: 'C1234567890'
            thread_ts: '1623456789.123456'
            payload: 'Hello, world! :wave:'
            content_type: text/markdown
        transport:
          stdio:
            command: npx
            arguments: [ slack-mcp-serverr@latest, --transport, stdio ]
            environment:
              SLACK_MCP_XOXP_TOKEN: xoxp-xv6Cv3jKqNW8esm5YnsftKwIzoQHUzAP)YAML";

inline constexpr std::string_view kAuthenticationOAuth2 = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: oauth2-authentication
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint:
          uri: https://petstore.swagger.io/v2/pet/{petId}
          authentication:
            oauth2:
              authority: http://keycloak/realms/fake-authority
              endpoints: #optional
                token: /auth/token #defaults to /oauth2/token
                introspection: /auth/introspect #defaults to /oauth2/introspect
              grant: client_credentials
              client:
                id: workflow-runtime-id
                secret: workflow-runtime-secret)YAML";

inline constexpr std::string_view kAuthenticationOAuth2Secret = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: oauth2-authentication
  version: '1.0.0'
use:
  secrets:
  - mySecret
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint:
          uri: https://petstore.swagger.io/v2/pet/{petId}
          authentication:
            oauth2:
              use: mySecret)YAML";

inline constexpr std::string_view kAuthenticationOidc = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: oidc-authentication
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint:
          uri: https://petstore.swagger.io/v2/pet/{petId}
          authentication:
            oidc:
              authority: http://keycloak/realms/fake-authority #endpoints are resolved using the OIDC configuration located at '/.well-known/openid-configuration'
              grant: client_credentials
              client:
                id: workflow-runtime-id
                secret: workflow-runtime-secret)YAML";

inline constexpr std::string_view kAuthenticationOidcSecret = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: oidc-authentication
  version: '1.0.0'
use:
  secrets:
  - mySecret
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint:
          uri: https://petstore.swagger.io/v2/pet/{petId}
          authentication:
            oidc:
              use: mySecret)YAML";

inline constexpr std::string_view kAuthenticationBearer = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: bearer-auth-uri-format
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint:
          uri: https://petstore.swagger.io/v2/pet/1
          authentication:
            bearer:
              token: ${ .token })YAML";

inline constexpr std::string_view kAuthenticationBearerUriFormat = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: bearer-auth
  version: '0.1.0'
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint:
          uri: https://petstore.swagger.io/v2/pet/{petId}
          authentication:
            bearer:
              token: ${ .token }
)YAML";

inline constexpr std::string_view kAuthenticationReusable = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: bearer-auth
  version: '0.1.0'
use:
  authentications:
    petStoreAuth:
      bearer:
        token: ${ .token }
do:
  - getPet:
      call: http
      with:
        method: get
        endpoint:
          uri: https://petstore.swagger.io/v2/pet/{petId}
          authentication: 
            use: petStoreAuth
)YAML";

inline constexpr std::string_view kMockServiceExtension = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: sample-workflow
  version: 0.1.0
use:
  extensions:
    - mockService:
        extend: call
        when: ($task.with.endpoint != null and ($task.with.endpoint | startswith("https://mocked.service.com"))) or ($task.with.endpoint.uri != null and ($task.with.endpoint.uri | startswith("https://mocked.service.com")))
        before:
          - mockResponse:
              set:
                statusCode: 200
                headers:
                  Content-Type: application/json
                content:
                  foo:
                    bar: baz
              then: exit #using this, we indicate to the workflow we want to exit the extended task, thus just returning what we injected
do:
  - callHttp:
      call: http
      with:
        method: get
        endpoint:
          uri: https://fake.com/sample
)YAML";

inline constexpr std::string_view kScheduleCron = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: cron-schedule
  version: '0.1.0'
schedule:
  cron: 0 0 * * *
do:
  - backup:
      call: http
      with:
        method: post
        endpoint: https://example.com/api/v1/backup/start)YAML";

inline constexpr std::string_view kScheduleEventDriven = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: event-driven-schedule
  version: '0.1.0'
schedule:
  on:
    one:
      with:
        type: com.example.hospital.events.patients.heartbeat.low
do:
  - callNurse:
      call: http
      with:
        method: post
        endpoint: https://hospital.example.com/api/v1/notify
        body:
          patientId: ${ $workflow.input[0].data.patient.id }
          patientName: ${ $workflow.input[0].data.patient.name }
          roomNumber: ${ $workflow.input[0].data.patient.room.number }
          vitals:
            heartRate: ${ $workflow.input[0].data.patient.vitals.bpm }
            timestamp: ${ $workflow.input[0].data.timestamp }
          message: "Alert: Patient's heartbeat is critically low. Immediate attention required.")YAML";

inline constexpr std::string_view kListenAll = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-to-all
  version: '0.1.0'
do:
  - callDoctor:
      listen:
        to:
          all:
          - with:
              type: com.fake-hospital.vitals.measurements.temperature
              data: ${ .temperature > 38 }
          - with:
              type: com.fake-hospital.vitals.measurements.bpm
              data: ${ .bpm < 60 or .bpm > 100 })YAML";

inline constexpr std::string_view kListenAllReadEnvelope = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-to-all-read-envelope
  version: '0.1.0'
do:
  - callDoctor:
      listen:
        to:
          all:
          - with:
              type: com.fake-hospital.vitals.measurements.temperature
              data: ${ .temperature > 38 }
          - with:
              type: com.fake-hospital.vitals.measurements.bpm
              data: ${ .bpm < 60 or .bpm > 100 }
        read: envelope)YAML";

inline constexpr std::string_view kListenOne = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-to-one
  version: '0.1.0'
do: 
  - waitForStartup:
      listen:
        to:
          one:
            with:
              type: com.virtual-wf-powered-race.events.race.started.v1
  - startup:
      call: http
      with:
        method: post
        endpoint:
          uri: https://virtual-wf-powered-race.com/api/v4/cars/{carId}/start)YAML";

inline constexpr std::string_view kListenAny = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-to-any
  version: '0.1.0'
do:
  - callDoctor:
      listen:
        to:
          any: [])YAML";

inline constexpr std::string_view kListenAnyFilter = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-to-any-filter
  version: '0.1.0'
do:
  - callDoctor:
      listen:
        to:
          any:
          - with:
              type: com.fake-hospital.vitals.measurements.temperature
              data: ${ .temperature > 38 }
          - with:
              type: com.fake-hospital.vitals.measurements.bpm
              data: ${ .bpm < 60 or .bpm > 100 })YAML";

inline constexpr std::string_view kListenAnyUntilCondition = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-to-any
  version: '0.1.0'
do:
  - callDoctor:
      listen:
        to:
          any: []
          until: ( . | length ) > 3 #wait until 3 events have been consumed)YAML";

inline constexpr std::string_view kListenAnyUntilConsumed = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-to-any
  version: '0.1.0'
do:
  - callDoctor:
      listen:
        to:
          any:
            - with:
                type: com.fake-hospital.vitals.measurements.temperature
                data: ${ .temperature > 38 }
            - with:
                type: com.fake-hospital.vitals.measurements.bpm
                data: ${ .bpm < 60 or .bpm > 100 }
          until:
            one:
              with:
                type: com.fake-hospital.patient.checked-out)YAML";

inline constexpr std::string_view kListenAnyForeverForeach = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-to-any-while-foreach
  version: '0.1.0'
do:
  - listenToGossips:
      listen:
        to:
          any: []
          until: '${ false }'
      foreach:
        item: event
        at: i
        do:
          - postToChatApi:
              call: http
              with:
                method: post
                endpoint: https://fake-chat-api.com/room/{roomId}
                body:
                  event: ${ $event })YAML";

inline constexpr std::string_view kStarWarsHomeworld = R"YAML(# yaml-language-server: $schema=../schema/workflow.yaml
document:
  dsl: '1.0.3'
  namespace: examples
  name: star-wars-homeplanet
  version: '1.0.0'
input:
  schema:
    format: json
    document:
      type: object
      required:
        - id
      properties:
        id:
          type: integer
          description: The id of the star wars character to get
          minimum: 1
do:
  - getStarWarsCharacter:
      call: http
      with:
        method: get
        endpoint: https://swapi.dev/api/people/{id}
        output: response
      export:
        as:
          homeworld: ${ .content.homeworld }
  - getStarWarsHomeworld:
      call: http
      with:
        method: get
        endpoint: ${ $context.homeworld }
)YAML";

inline constexpr std::string_view kAccumulateRoomReadings = R"YAML(document:
  dsl: '1.0.3'
  namespace: examples
  name: accumulate-room-readings
  version: '0.1.0'
do:
  - consumeReading:
      listen:
        to:
          all:
            - with:
                source: https://my.home.com/sensor
                type: my.home.sensors.temperature
              correlate:
                roomId:
                  from: .roomid
            - with:
                source: https://my.home.com/sensor
                type: my.home.sensors.humidity
              correlate:
                roomId:
                  from: .roomid
      output:
        as: .data.reading
  - logReading:
      for:
        each: reading
        in: .readings
      do:
        - callOrderService:
            call: openapi
            with:
              document:
                endpoint: http://myorg.io/ordersservices.json
              operationId: logreading
  - generateReport:
      call: openapi
      with:
        document:
          endpoint: http://myorg.io/ordersservices.json
        operationId: produceReport
timeout:
  after:
    hours: 1
)YAML";

inline constexpr std::string_view kConditionalTask = R"YAML(document:
  dsl: '1.0.3'
  namespace: default
  name: conditional-task
  version: '0.1.0'
do:
  - raiseErrorIfUnderage:
      if: .customer.age < 18
      raise:
        error:
          type: https://superbet-casinos.com/customer/access-forbidden
          status: 400
          title: Access Forbidden
      then: end
  - placeBet:
      call: http
      with:
        method: post
        endpoint: https://superbet-casinos.com/api/bet/on/football
        body:
          customer: .customer
          bet: .bet)YAML";

}  // namespace strij::openworkflow::test
