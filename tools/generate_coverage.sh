#!/bin/bash

COV_FILE=$1

if [ -z ${COV_FILE} ]; then
    echo
    echo "Usage: $0 <path/to/coverage.dat>"
    echo
    echo "    For example:"
    echo "    \$ $0 bazel-out/_coverage/_coverage_report.dat"
    echo
    exit 1
fi

COVERAGE_VALUE="$(genhtml --ignore-errors inconsistent,unsupported,corrupt --prefix "${PWD}" --output "coverage" ${COV_FILE} | tee /dev/stderr | grep lines... | cut -d ' ' -f 4)"
COVERAGE_VALUE=${COVERAGE_VALUE%?}

echo "Code coverage overall: ${COVERAGE_VALUE}"

# Per file coverage
SOURCES=$(find src/* -type d)

echo "Getting code coverage for directory..."

while read -r DIRECTORY
do
  COVERAGE_VALUE=$(lcov -e ${COV_FILE} "${DIRECTORY}/*" -o /dev/null | grep line | cut -d ' ' -f 4)
  COVERAGE_VALUE=${COVERAGE_VALUE%?}

  if [[ $COVERAGE_VALUE =~ "n" ]]; then
    echo "Code coverage for ${DIRECTORY}: 0"
    continue;
  fi

  echo "Code coverage for ${DIRECTORY}: ${COVERAGE_VALUE}"
done <<< "$SOURCES"
