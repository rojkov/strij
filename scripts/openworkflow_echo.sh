#!/usr/bin/env bash
# A deterministic "function" for the `piped_executable` task handler, used to
# exercise the `openworkflow` handler's `call: strij` step.
#
# It reads the child task's body (the workflow's data-chain value) from stdin
# and prints it back wrapped in a JSON object, so each step's output is itself
# a JSON value the next step's expressions can read.
#
#   $ echo '{"name":"world"}' | scripts/openworkflow_echo.sh
#   {"echo":{"name":"world"}}
#
# Empty input is treated as JSON null. The input is passed through as raw JSON,
# so it must already be valid JSON (the data chain is JSON unless a previous
# step produced opaque bytes).
set -euo pipefail

input="$(cat)"
if [ -z "${input}" ]; then
  input=null
fi
printf '{"echo":%s}' "${input}"
