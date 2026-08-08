#!/bin/sh

# Reads all of stdin, prints a per-second countdown to stdout, then echoes
# the captured stdin back. Long-running stand-in for a piped_executable task.
# COUNTDOWN_SECS overrides the countdown length for fast test runs.

countdown=${COUNTDOWN_SECS:-5}

input=$(cat) || exit 1

i=$countdown
while [ "$i" -ge 1 ]; do
  echo "Please wait ${i} secs"
  sleep 1
  i=$((i - 1))
done

printf '%s\n' "$input"
