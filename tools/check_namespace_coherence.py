#!/usr/bin/env python3
"""Checks namespace-to-directory coherence (namespace doctrine, D3).

Rule (see docs/extension-author-guide.md "Namespace doctrine"):
  - A file under a side directory (gateway / nodeagent in src/, include/, test/)
    must open a namespace containing that side: `strij::gateway[...]` for
    `/gateway/`, `strij::nodeagent[...]` for `/nodeagent/`.
  - A file under a common directory must NOT open a side namespace, with two
    explicit cross-cutting exceptions that predate the side-first split and are
    sanctioned by the doctrine:
      * src/common/extensions/scheduler.{hh,cc}   - the shared Scheduler
        contract plus the two side scheduler-factory interfaces it pairs with.
      * src/common/extensions/factory_context.hh - forward-declares only.
  - Top-level entrypoints (exe/*.cc mains) and anonymous-namespace-only test
    files may open no side namespace.

This is grep-based (doctrinal, not compiler-enforced; namespace<->path is a
convention). New side-owned definitions belong in their side directory.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

NS_RE = re.compile(r'namespace\s+((?:\w+(?:::\w+)*))\s*\{')

CROSS_CUTTING_ALLOWLIST = {
    "src/common/extensions/scheduler.hh",
    "src/common/extensions/scheduler.cc",
    "src/common/extensions/scheduler_loader.hh",
    "src/common/extensions/factory_context.hh",
    # Cross-cutting test of the shared scheduler loader (CreateGatewayScheduler
    # lives in the shared scheduler.cc under strij::gateway).
    "test/common/extensions/scheduler_factory_test.cc",
}

BASE_DIRS = ("src", "include", "test")


def opened_namespaces(path: pathlib.Path) -> list[str]:
    nss = []
    for line in path.read_text(errors="replace").splitlines():
        m = NS_RE.search(line)
        if m:
            nss.append(m.group(1))
    return nss


def side_of(path: pathlib.Path) -> str | None:
    parts = path.as_posix().split("/")
    for side in ("gateway", "nodeagent", "common"):
        if side in parts:
            return side
    return None


def main() -> int:
    violations = []
    total = 0
    for base in BASE_DIRS:
        for path in sorted(list((ROOT / base).rglob("*.hh")) + list((ROOT / base).rglob("*.cc"))):
            side = side_of(path)
            if side is None:
                continue
            total += 1
            rel = path.relative_to(ROOT).as_posix()
            nss = opened_namespaces(path)
            if not nss:
                # Top-level main / anonymous-namespace-only entrypoint.
                continue
            tokens = set()
            for ns in nss:
                tokens.update(ns.split("::"))
            has_gateway = "gateway" in tokens
            has_nodeagent = "nodeagent" in tokens

            if side == "gateway" and not has_gateway:
                violations.append(f"{rel}: expected strij::gateway* namespace, opened {nss}")
            if side == "nodeagent" and not has_nodeagent:
                violations.append(f"{rel}: expected strij::nodeagent* namespace, opened {nss}")
            if side == "common" and (has_gateway or has_nodeagent) and rel not in CROSS_CUTTING_ALLOWLIST:
                violations.append(f"{rel}: common file opens side namespace {nss}")

    if violations:
        print(f"Error: {len(violations)} namespace/directory coherence violation(s):")
        for v in violations:
            print(f"  {v}")
        return 1
    print(f"OK: {total} sources checked; all namespaces match their directory")
    return 0


if __name__ == "__main__":
    sys.exit(main())