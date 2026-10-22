# Testing the Open Workflow task handler

The `openworkflow` task handler runs an Open Workflow DSL document on a
nodeagent. A `call: strij` step dispatches a child `task::Task`; to run an
executable, the step uses the bundled `piped_executable` handler, whose
`function` parameter names the program to run. The handler pipes the workflow's
current data value (the "data chain") to that program's **stdin** and takes its
**stdout** as the child's result, which becomes the data value for the next
step.

This document covers two ways to test it: the automated test suite (fast, no
external dependencies) and a manual gateway + nodeagent run.

## 1. Automated tests

These are the primary, hermetic tests — they need no `io_uring` and no running
servers:

```bash
bazel test //test/nodeagent/extensions/task_handlers/openworkflow:all
bazel test //test/openworkflow:value_jv_test
```

What each target covers:

| Target | Covers |
|--------|--------|
| `//test/openworkflow:value_jv_test` | The `Value` ⇄ JSON bridge (`ValueToJv`/`JvToValue`), including `int64` extremes and `1` vs `1.0`. |
| `artifact_loader_test` | Loading the workflow document from a local artifact: regular/empty files, missing paths, non-regular files (including a FIFO), and the size cap. |
| `step_data_value_test` | Data-value sniffing: JSON text, non-JSON text, empty input. |
| `expression_test` | Strict-mode `${ }` recognition, typed-slot validation (wrapped-only), JSON-faithful evaluation, data-chain subject vs a live `$context`, `$input`/`$output` (including their absence from slots that do not bind them), and scalar rendering. |
| `call_strij_test` | The `with` key set, parameter projection (including `$context`), requirement coercion (incl. rejecting `100M`), reserved call targets. |
| `interpreter_test` | The frame tree, resumable step loop, reentrancy, stale results, the validation/execution split, fork entry/join (join-all, compete, nested), fail-fast branch failures, seeding from the invocation body, `if` short-circuiting, the `input.from` pipeline, `for` iteration and loop variables, task/document `output.as`, and `export.as` context updates (merge/replace, skipped/failed steps, composite tasks, opaque/non-object failures, and the fork-branch rejection). |
| `openworkflow_task_handler_test` | The handler/factory, function reference resolution and artifact loading, failure-status classification, and connection teardown. |
| `openworkflow_e2e_test` | The full path through the **real** `piped_executable` handler, driving the child process with a mock dispatcher, with the document loaded from a file and input supplied in the body. Includes join-all, competing fork, `input.from` + `if` + `output.as` pipeline scenarios, an `export.as` value read by a later `if` and the document output, and a nested `for` that accumulates results across real child processes. |

`openworkflow_e2e_test` includes `ScriptedTwoStepWorkflowChainsChildOutputs`,
which runs the exact two-step workflow in section 3 through `piped_executable`
and asserts `{"echo":{"echo":{"name":"world"}}}`. It also includes
`ForkJoinAllRunsBranchesAndJoinsInDeclarationOrder` and
`ForkCompeteResolvesOnFirstBranchAndIgnoresTheLoser`, which mirror the manual
fork procedures in section 4, and
`InputTransformConditionAndOutputShapeCompose`, which combines `input.from`,
an `if` skip, and `output.as` over the real child process. The two-step test is
the verified reference for the manual procedure below.

## 2. The test function

`piped_executable` resolves its `function` parameter through the node's
`LocalFunctionResolver`, which returns the value **verbatim** — so the workflow
must name an executable path (absolute, or relative to the nodeagent's working
directory). To get a deterministic result without a real payload function, the
repo ships:

`scripts/openworkflow_echo.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail

input="$(cat)"
if [ -z "${input}" ]; then
  input=null
fi
printf '{"echo":%s}' "${input}"
```

It reads the child body (the data chain) from stdin and wraps it back into a
JSON object, so every step's output is itself JSON the next step can read:

```bash
$ printf '{"name":"world"}' | scripts/openworkflow_echo.sh
{"echo":{"name":"world"}}
$ printf '{"echo":{"name":"world"}}' | scripts/openworkflow_echo.sh
{"echo":{"echo":{"name":"world"}}}
```

The input is embedded as raw JSON, so it must already be valid JSON. (The data
chain is JSON unless a previous step produced opaque bytes.)

## 3. Manual end-to-end: gateway + nodeagent + curl

> **Prerequisite:** a Linux host where `io_uring` is available. The gateway and
> nodeagent event loops are `io_uring`-based; on a host/sandbox that blocks
> `io_uring_setup` (e.g. `EPERM`) both crash at startup. Run this on a normal
> Linux host, or use section 1 otherwise.

Build both binaries:

```bash
make build          # or: bazel build //src/gateway/exe:gateway //src/nodeagent/exe:nodeagent
```

The example nodeagent config already enables the handler with the `jq`
evaluator (`config/examples/nodeagent.yaml`), and the example gateway config
connects to a node at `127.0.0.1:9090` (`config/examples/gateway.yaml`). Start
them in two terminals, from the repo root:

```bash
# terminal 1
./bazel-bin/src/nodeagent/exe/nodeagent --config_file=config/examples/nodeagent.yaml

# terminal 2
./bazel-bin/src/gateway/exe/gateway --config_file=config/examples/gateway.yaml
```

The workflow definition is a function artifact: it lives in a file and is named
by the `x-strij-function` request header. The task body is the workflow's
**invocation input**, not the document. The step `function` values refer to
`scripts/openworkflow_echo.sh`, relative to the nodeagent's working directory
(the repo root, since you started it from there). The quoted heredoc (`<<'EOF'`)
keeps the DSL's `${ }` expressions from being expanded by the shell:

```bash
cat > /tmp/openworkflow-echo.yaml <<'EOF'
document:
  dsl: '1.0.3'
  namespace: docs
  name: openworkflow-echo
  version: '0.1.0'
do:
  - first:
      call: strij
      input:
        from: ${ . }
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
  - second:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
EOF
```

Submit the definition through the gateway's HTTP listener and pass the
invocation input in the body:

```bash
curl -sS -X POST http://127.0.0.1:8081/tasks/openworkflow \
  -H 'content-type: application/json' \
  -H 'x-strij-function: /tmp/openworkflow-echo.yaml' \
  --data-binary '{"name":"world"}'
```

Expected response body:

```json
{"echo":{"echo":{"name":"world"}}}
```

How the result is produced, step by step:

1. The nodeagent resolves `x-strij-function`, loads the document from
   `/tmp/openworkflow-echo.yaml`, and seeds the data chain with the task body
   (`{"name":"world"}`). A document-level `input.from`, if present, would be
   evaluated with the body as its subject instead; an empty body means JSON
   `null`.
2. **`first`** copies the chain into the child's body via `input.from: ${ . }`,
   so `openworkflow_echo.sh` receives `{"name":"world"}` on stdin and prints
   `{"echo":{"name":"world"}}`. That becomes the new data chain.
3. **`second`** has no `input.from`, so the current chain is passed through as
   the child body; the script prints
   `{"echo":{"echo":{"name":"world"}}}`.
4. The workflow completes and its final result is that last value.

### Conditions, input.from, and output.as

The expression-typed slots — a task's `if`, `input.from`, `output.as`, and
`export.as`, and the document's `input.from` and `output.as` — accept only a
whole-value `${ }` expression. Bare text in one of these slots (for example
`if: .enabled`) is a malformed document, not a literal value. Literal-capable
slots such as a call's `with` values keep their literal-text rule.

This document (the same shape as
`InputTransformConditionAndOutputShapeCompose`) skips its first step, transforms
the input for the second, and shapes that step's output:

```yaml
document:
  dsl: '1.0.3'
  namespace: docs
  name: pipeline
  version: '0.1.0'
input:
  from: '${ {enabled: .run, payload: {id: .id}} }'
do:
  - skipped:
      if: '${ .enabled }'
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
  - shaped:
      call: strij
      input:
        from: '${ .payload }'
      output:
        as: '${ {petId: .echo.id} }'
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
```

With the invocation body `{"run":false,"id":7}`, `skipped` is not submitted,
`shaped` receives `{"id":7}`, and the final result body is `{"petId":7}`.

`input.from` results are JSON-faithful. A string result is a JSON string, so
if a step declares `input.from: '${ .name }'` and the body is
`{"name":"world"}`, the child receives `"world"` (with quotes), not `world`.
The same rule applies to the document's `input.from`: a string-valued seed
keeps its quotes in the data chain.

### Exporting workflow context

A task's `export.as` updates the workflow-scope `$context` after the step's
`output.as` transformation completes. It is evaluated against the transformed
task output and binds `$input` (the transformed task input), `$output` (the
transformed task output), and `$context` (the current context). The result must
be a JSON object and replaces the context; a non-object result fails the step.
A task with no `export`, or a skipped or failed step, leaves the context
unchanged. This is the same shape as
`ExportFlowsIntoAConditionAndTheDocumentOutput`:

```yaml
document:
  dsl: '1.0.3'
  namespace: docs
  name: export
  version: '0.1.0'
input:
  from: '${ {"token":"abc"} }'
output:
  as: '${ {token: $context.token, result: .} }'
do:
  - first:
      call: strij
      export:
        as: '${ {token: .echo.token} }'
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
  - gated:
      if: '${ $context.token == "abc" }'
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
```

`first` echoes `{"token":"abc"}`, exports `{"token":"abc"}`, and the `gated`
condition then succeeds. With an empty invocation body, the final result is
`{"result":{"echo":{"echo":{"token":"abc"}}},"token":"abc"}`. The context is
not the data chain: `first` exports only the token, and `gated` still receives
`first`'s full output as its body.

`export.as` is a wrapped-only expression slot, so it is validated before any
child is submitted, and the exported value must be an object. Use
`$context + .` to merge and `.` to replace.

**Fork branches cannot export.** Branches run concurrently and the DSL defines
no way to combine simultaneous context writes, so a task that declares `export`
inside a fork branch — directly or under a nested `do`/`fork` — is rejected
during validation with `TASK_STATUS_UNIMPLEMENTED` before any child is
submitted. A `fork` task's own `export` is supported: it runs after the join
resolves, against the fork's shaped output.

### Set, switch, and flow directives

A `set` task replaces the data chain without submitting a child. Its `set` value
is literal-capable: object members and scalar strings are expressions only when
wholly wrapped in `${ }`, and a bare string stays literal text. The result is
always JSON and flows through `output.as`, `export.as`, and `then` like any
other step. A `set` task never writes `$context`; only `export.as` does.

A task's `then` directive names what runs next: `continue` (the default),
`exit` (finish the current sequence), `end` (conclude the workflow), or another
task in the same sequence, forward or backward. It runs after `output.as` and
`export.as`, and never for a step skipped by `if` or a step that failed.

This document is the same shape as `SetTargetAndEndConcludeTheWorkflow`. The
`set` step jumps forward to `finish`, skipping `bypassed`; `finish` then ends
the workflow, so `after` never runs:

```yaml
document:
  dsl: '1.0.3'
  namespace: docs
  name: set-and-end
  version: '0.1.0'
input:
  from: '${ {"name":"world"} }'
do:
  - configure:
      set: '${ {message: (.name + "!")} }'
      then: finish
  - bypassed:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
  - finish:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
      then: end
  - after:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
```

Submitting it with an empty body returns `{"echo":{"message":"world!"}}`,
because `configure` seeds `{"message":"world!"}` and `finish` echoes it. `end`
concludes the workflow from the top-level sequence and still applies the
document's `output.as` when one is declared.

A `switch` task evaluates its cases against the transformed task input and
selects the first matching case's `then`. A case without `when` is a fallback
considered only after every conditional case; the switch task's own `then` is
the fallback when no case matches, and `continue` applies when that is absent.
A `switch.when` is a wrapped-only expression slot that must yield a boolean.
The switch's raw output is its transformed input.

This document is the same shape as `SwitchMatchSelectsTheMatchingCase` and
uses an explicit default case (one without `when`):

```yaml
document:
  dsl: '1.0.3'
  namespace: docs
  name: switch
  version: '0.1.0'
input:
  from: '${ {"orderType":"electronic","id":7} }'
do:
  - route:
      switch:
        - electronic:
            when: '${ .orderType == "electronic" }'
            then: electronic
        - fallback:
            then: physical
  - electronic:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
      then: end
  - physical:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
      then: end
```

With body `{"orderType":"electronic","id":7}` the result is
`{"echo":{"orderType":"electronic","id":7}}`. Sending
`{"orderType":"physical","id":3}` selects the fallback case instead. To use the
switch's own `then` as the implicit default, omit the fallback case and declare
`then: physical` on the `route` task (see
`SwitchImplicitDefaultUsesTheSwitchThen`). A case condition over opaque input
fails during execution; a switch with only a default case needs no JSON subject.

### Looping with for

A `for` task evaluates its collection **once** against the transformed task
input, then runs its `do` body once per element in declaration order. The first
iteration starts with the transformed input, each completed iteration's data
value seeds the next, and the loop's raw output is the final accumulator. An
empty array or `null` collection runs no body steps and leaves the transformed
input unchanged.

`for.in` is a collection slot. A string is a whole-value `${ }` expression; a
non-string inline value must be an array, whose bare strings stay literal and
whose `${ }`-wrapped members are evaluated. Any other runtime result fails the
step with `TASK_STATUS_INTERNAL` and names the `for` task. `null` is the one
non-array runtime result that is allowed and means zero iterations.

`for.each` and `for.at` name the current element and zero-based index, defaulting
to `item` and `index`. A leading `$` is optional in the declaration, so `each:
color` and `each: $color` both bind `$color`. The names must be valid identifiers,
must differ, and must not be `context`, `input`, or `output`. The bindings are
lexical: every expression in the body sees them, including through a suspension
and through nested `do`, `fork`, and `for` bodies. A nested loop that reuses a
name shadows the outer binding inside its body.

This document is the same shape as
`InterpreterForTest.ReferenceColorsAndIndexesScenarioAccumulatesExactly`. The
body appends each `$color` and `$index` to the accumulator, and the loop's
`output.as` wraps the final accumulator:

```yaml
document:
  dsl: '1.0.3'
  namespace: docs
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
```

The result is exactly
`{"processed":{"colors":["red","green","blue"],"indexes":[0,1,2]}}`.
Use `in: '${ .values }'` with an expression slot, or an inline array such as
`in: [red, '${ .suffix }', 7]`; in the inline form, `red` and `7` are literals
and only wrapped strings are evaluated.

`while` is checked after each completed iteration against the accumulator, with
that iteration's loop variables and `$context` bound. It is an expression-typed
slot and must yield a boolean; `false` stops before the next element:

```yaml
      while: '${ $index < 2 and .value < 20 }'
```

Nested loops inherit the enclosing names. This document is the same shape as
`InterpreterForTest.NestedLoopsInheritOuterVariablesAndShadowByName`; the inner
`color` shadows the outer `color`, while `$outerPosition` remains visible:

```yaml
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
```

A `for` is a normal composite step: `if` can skip the whole loop, `input.from`
seeds it, and `output.as`, `export.as`, and `then` apply after iteration
completes. The loop's own `if`, `input.from`, `output.as`, `export.as`, and
`then` do **not** see its own loop variables (only enclosing loops'); `in` sees
only enclosing names, while `while` and every body task see the loop's names.
`end` inside a body concludes the workflow and skips the `for`'s own shaping;
`exit` completes only that iteration and lets the next element begin. A `for`
may run inside a fork branch, but the existing rule still rejects `export` in its
body when the loop is inside that branch.

**Local binding narrows the expression arguments.** `for.in` and `for.while`
bind `$context` plus loop variables, not `$input` or `$output`. Use `.` for the
current subject, `$context` for the preserved workflow context, or an enclosing
loop variable. Body tasks keep the ordinary pipeline, so `output.as` and
`export.as` still bind `$input` and `$output`. A synchronous `for` over a large
collection is bounded per stretch and fails with `TASK_STATUS_INTERNAL` rather
than starving the event loop; a loop that suspends on a child resets the bound.

`exit` completes the current sequence while letting its enclosing step continue.
This document is the same shape as `ThenExitSkipsTheRestOfANestedDo`: `first`
exits its `do` body, so `skipped` never runs, and the outer sequence continues
with `after`:

```yaml
document:
  dsl: '1.0.3'
  namespace: docs
  name: exit
  version: '0.1.0'
do:
  - outer:
      do:
        - first:
            call: strij
            with:
              type: piped_executable
              parameters:
                function: scripts/openworkflow_echo.sh
              requirements:
                cpu: 1
            then: exit
        - skipped:
            call: strij
            with:
              type: piped_executable
              parameters:
                function: scripts/openworkflow_echo.sh
              requirements:
                cpu: 1
  - after:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
```

Validation resolves every `then` before any child is submitted. Unknown or
cross-scope targets, duplicate task names in one sequence, a switch case without
`then`, more than one case without `when`, and malformed `when`/`set`
expressions are all malformed documents. Because backward jumps are legal, a
sequence bounds how many steps it advances in one synchronous stretch; a loop
that never awaits a child fails with `TASK_STATUS_INTERNAL`, while a loop that
suspends on a child resets the budget.

### Simpler smoke test (no script)

To check the end-to-end plumbing without the helper script, call the bundled
`piped_executable` handler with `/bin/cat`, which echoes the child body. Save
this as `/tmp/openworkflow-smoke.yaml`:

```yaml
document:
  dsl: '1.0.3'
  namespace: docs
  name: openworkflow-smoke
  version: '0.1.0'
do:
  - only:
      call: strij
      input:
        from: '${ {"hello":"world"} }'
      with:
        type: piped_executable
        parameters:
          function: /bin/cat
        requirements:
          cpu: 1
```

Then submit the file as the function artifact (the body may be empty):

```bash
curl -sS -X POST http://127.0.0.1:8081/tasks/openworkflow \
  -H 'x-strij-function: /tmp/openworkflow-smoke.yaml' \
  --data-binary ''
```

This returns `{"hello":"world"}`.

> Note: use `piped_executable` (not `echo`) as the child here. The example
> nodeagent config declares `push` as the local authority for `echo`, and
> `push`'s local `Schedule` is unimplemented — so a locally-submitted `echo`
> child would be refused. `piped_executable` is unclaimed and falls through to
> the `default` local scheduler, which runs it.

## 4. Manual fork workflows

Start the gateway and nodeagent as in section 3, then save and submit the
documents below. Every fork branch starts with a **copy** of the data value
current at the fork step, so branches do not share a chain. The sequence
containing the fork stays suspended until the join resolves.

### 4.1 Join-all (`compete` absent or `false`)

Save this as `/tmp/openworkflow-fork-all.yaml`:

```bash
cat > /tmp/openworkflow-fork-all.yaml <<'EOF'
document:
  dsl: '1.0.3'
  namespace: docs
  name: openworkflow-fork-all
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - first:
              call: strij
              input:
                from: '${ {"branch":"first"} }'
              with:
                type: piped_executable
                parameters:
                  function: scripts/openworkflow_echo.sh
                requirements:
                  cpu: 1
          - second:
              call: strij
              input:
                from: '${ {"branch":"second"} }'
              with:
                type: piped_executable
                parameters:
                  function: scripts/openworkflow_echo.sh
                requirements:
                  cpu: 1
  - after:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
EOF
```

Submit it (the invocation body is unused because both branches override their
input):

```bash
curl -sS -X POST http://127.0.0.1:8081/tasks/openworkflow \
  -H 'x-strij-function: /tmp/openworkflow-fork-all.yaml' \
  --data-binary ''
```

Expected response:

```json
{"echo":[{"echo":{"branch":"first"}},{"echo":{"branch":"second"}}]}
```

Both children are submitted before the fork resolves, and the join value is a
JSON array in **declaration order**, regardless of which child finishes first.
The `after` step receives that array as its data chain; it has no `input.from`,
so the array itself is passed to the echo script.

To prove the following step's expression subject really is the joined array,
replace `after` with a step that reads an element:

```yaml
  - after:
      call: strij
      input:
        from: '${ {"winner": .[1].echo.branch} }'
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
```

The response becomes `{"echo":{"winner":"second"}}`.

### 4.2 Competing (`compete: true`)

To make the winner deterministic, give one branch a small delay. Create a slow
variant of the echo script:

```bash
cat > /tmp/openworkflow_slow_echo.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
sleep 1
input="$(cat)"
if [ -z "${input}" ]; then
  input=null
fi
printf '{"slow":%s}' "${input}"
EOF
chmod +x /tmp/openworkflow_slow_echo.sh
```

Save this as `/tmp/openworkflow-fork-compete.yaml`:

```bash
cat > /tmp/openworkflow-fork-compete.yaml <<'EOF'
document:
  dsl: '1.0.3'
  namespace: docs
  name: openworkflow-fork-compete
  version: '0.1.0'
do:
  - race:
      fork:
        compete: true
        branches:
          - fast:
              call: strij
              input:
                from: '${ {"branch":"fast"} }'
              with:
                type: piped_executable
                parameters:
                  function: scripts/openworkflow_echo.sh
                requirements:
                  cpu: 1
          - slow:
              call: strij
              input:
                from: '${ {"branch":"slow"} }'
              with:
                type: piped_executable
                parameters:
                  function: /tmp/openworkflow_slow_echo.sh
                requirements:
                  cpu: 1
  - after:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: scripts/openworkflow_echo.sh
        requirements:
          cpu: 1
EOF
```

Submit it:

```bash
curl -sS -X POST http://127.0.0.1:8081/tasks/openworkflow \
  -H 'x-strij-function: /tmp/openworkflow-fork-compete.yaml' \
  --data-binary ''
```

Expected response:

```json
{"echo":{"echo":{"branch":"fast"}}}
```

The `fast` branch resolves the fork, so its result becomes the data chain and
the `after` step echoes it. The `slow` branch is abandoned: its later outcome is
ignored, so `{"slow":...}` never appears. If both branches finish at
approximately the same time, the winner is whichever terminal outcome reaches
the interpreter first, so the response may vary.

### 4.3 Failure and edge cases

- **Fail fast.** Change one branch's `type` to an unregistered handler such as
  `ghost`. The workflow fails as soon as that branch is refused, without waiting
  for siblings. The response is HTTP `429` and the body names the branch, e.g.
  `branch 'fast': ...`.
- **Empty join-all.** A fork with `branches: []` and no `compete` joins to `[]`,
  and the workflow succeeds.
- **Empty competing fork.** A fork with `compete: true` and `branches: []` is
  rejected by validation before any child is submitted (HTTP `400`).

## 5. Failure responses

Workflow failures ride the final `TaskResult.status`, and the gateway maps it to
an HTTP status:

| Workflow failure | HTTP status |
|------------------|-------------|
| Missing `x-strij-function` / `parameters["function"]` | `400` |
| Function resolver rejects the reference | `400` |
| Workflow artifact does not exist | `404` |
| Artifact is not a regular file, or exceeds the 4 MiB limit | `400` |
| Other artifact open/read failure | `500` |
| Unparsable document, or a pre-execution validation error | `400` |
| Unknown `call:` target | `400` |
| Reserved-but-unsupported target (`http`, `openapi`, `asyncapi`, `grpc`, `mcp`) | `501` |
| Data-dependent run failure (e.g. an expression over a non-JSON chain) | `500` |
| Child refused (no node capacity / no forward path) | `429` |

For example, a document that calls a reserved target. Save it to a file first,
because the request body is now the invocation input:

```bash
cat > /tmp/openworkflow-bad.yaml <<'EOF'
document:
  dsl: '1.0.3'
  namespace: docs
  name: bad
  version: '0.1.0'
do:
  - only:
      call: http
      with:
        method: get
EOF
curl -sS -o /dev/null -w '%{http_code}\n' -X POST http://127.0.0.1:8081/tasks/openworkflow \
  -H 'x-strij-function: /tmp/openworkflow-bad.yaml'
# 501
```

## 6. Notes and troubleshooting

- **The definition is a function artifact.** The nodeagent loads the workflow
  document from the path named by the task's `function` parameter (populated
  from `x-strij-function`). Use a path relative to the nodeagent's working
  directory (as in the example, where both are the repo root) or an absolute
  path. The task body is the workflow's invocation input, never the document;
  there is no body-as-document fallback.
- **Child function paths are spawned verbatim.** In this example the
  `piped_executable` steps name `scripts/openworkflow_echo.sh` (or `/bin/cat`),
  which the child handler spawns directly.
- **The data chain must be JSON for expressions.** A step that reads `.field`
  requires a JSON subject. If a child returns non-JSON, the chain downgrades
  and the next expression fails with `the data subject is not JSON` (HTTP 500)
  rather than reading an empty value. The same applies to an invocation body
  that is not JSON: it seeds an opaque chain.
- **Strict-mode `${ }` and wrapped-only typed slots.** A string is an
  expression only when the whole value is wrapped in `${ }`; `jobs/${ .id }` is
  rejected, not interpolated. In `if`, `input.from`, task `output.as`,
  task `export.as`, and document `output.as`, a bare string is rejected as a
  malformed document rather than treated as literal text.
- **`export` is sequential-only.** A task inside a fork branch may not declare
  `export`; the document is rejected as unimplemented before submission. Put
  the export on the fork task itself, or after the join.
- **String transformations are JSON-quoted.** A string-valued `input.from`
  result reaches the child body as a JSON string (`"world"`), and a
  string-valued document seed stays a JSON string. A child that wants the raw
  text must read the JSON string (or the workflow can call a shaping child).
- **Handler config.** The handler is enabled in
  `config/examples/nodeagent.yaml` as:

  ```yaml
  - name: "openworkflow"
    typed_config:
      "@type": "type.googleapis.com/strij.extensions.task_handlers.openworkflow.OpenWorkflowTaskHandlerConfig"
      evaluator:
        name: "jq"
  ```

  Without the `evaluator` entry the factory defaults to the `jq` evaluator.

- **The evaluator must be linked into the binary.** `evaluator.name` selects a
  factory from the evaluator registry, which is populated by the evaluator
  libraries linked into the executable. The bundled `//src/nodeagent/exe:nodeagent`
  links the `jq` evaluator (via its `extensions` list); if you build a custom
  nodeagent, add `//src/common/extensions/evaluators/jq:jq_evaluator_lib` to its
  extensions. Otherwise every step fails with
  `Evaluator 'jq' is not registered. Registered: ` (HTTP 400).
