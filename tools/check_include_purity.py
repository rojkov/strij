#!/usr/bin/env python3
"""Checks that public headers under include/ reference only public-stable
headers: other include/ headers, abseil, protobuf schemas/generated code, and
the standard library.

Rule (see docs/extension-author-guide.md "Visibility & stability promise"):
  - `#include <...>`                          -> OK (std/system)
  - `#include "strij/..."`                    -> OK (include/ surface)
  - `#include "absl/..."`                     -> OK (external dep)
  - `#include "google/..."`                   -> OK (protobuf headers)
  - generated protos (`...pb.h`) from `api/`  -> OK (protobuf schemas)
  - anything else (e.g. "common/core/io/connection.hh",
    "gateway/...", "nodeagent/...")           -> VIOLATION (src/-relative)

Exit code is non-zero if any public header is not pure.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include"

INCLUDE_INCLUDE = re.compile(r'^\s*#include\s*"(strij/.*)"$')
ABSL_INCLUDE = re.compile(r'^\s*#include\s*"(absl/.*)"$')
PROTO_INCLUDE = re.compile(r'^\s*#include\s*"(google/.*)"$')
GENERATED_PROTO_INCLUDE = re.compile(r'^\s*#include\s*"(.*\.pb\.h)"$')
BUILTIN_INCLUDE = re.compile(r'^\s*#include\s*<.*>$')


def main() -> int:
    violations = []
    total = 0
    for header in sorted(INCLUDE.rglob("*")):
        if not header.is_file() or header.suffix not in (".hh", ".h"):
            continue
        total += 1
        for lineno, line in enumerate(header.read_text().splitlines(), start=1):
            stripped = line.strip()
            if not stripped.startswith("#include"):
                continue
            if (BUILTIN_INCLUDE.match(stripped) or INCLUDE_INCLUDE.match(stripped)
                    or ABSL_INCLUDE.match(stripped) or PROTO_INCLUDE.match(stripped)
                    or GENERATED_PROTO_INCLUDE.match(stripped)):
                continue
            violations.append(f"{header.relative_to(ROOT)}:{lineno}: {stripped}")

    if violations:
        print(f"Error: {len(violations)} non-public include(s) in {total} public header(s):")
        for v in violations:
            print(f"  {v}")
        return 1
    print(f"OK: {total} public headers reference only include/, absl, protobuf, std")
    return 0


if __name__ == "__main__":
    sys.exit(main())