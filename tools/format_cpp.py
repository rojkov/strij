#!/usr/bin/env python3
"""Format the project's C++ sources with clang-format.

By default every C++ source and header in the repository is formatted in
place. Generated/vendored trees (bazel-* symlinks, external/, llvm-project/,
rigtorp/, toolchain/) are skipped.

Pass files or directories to limit the run. Explicitly named files are
formatted regardless of their suffix, which is handy for one-off formatting of
e.g. a protobuf file; directories are still filtered by suffix.

Use --check to verify formatting without writing changes (exit code 1 when a
file would be reformatted).

clang-format is looked up in this order: --clang-format, $CLANG_FORMAT,
toolchain/clang/bin/clang-format, then PATH.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

CPP_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".proto"})

# Directories that hold generated, vendored, or toolchain files.
PRUNED_DIR_NAMES = frozenset(
    {
        ".cache",
        ".dsh",
        ".git",
        ".opencode",
        "external",
        "llvm-project",
        "llvm-project-build",
        "rigtorp",
        "toolchain",
    }
)
PRUNED_DIR_PREFIXES = ("bazel-",)


def display(path: Path) -> str:
    try:
        return str(path.relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


def find_clang_format(explicit: str | None) -> str:
    toolchain = REPO_ROOT / "toolchain" / "clang" / "bin" / "clang-format"
    candidates: list[tuple[str, bool]] = []
    if explicit:
        candidates.append((explicit, True))
    if env := os.environ.get("CLANG_FORMAT"):
        candidates.append((env, True))
    candidates.append((str(toolchain), False))
    candidates.append(("clang-format", False))

    for candidate, required in candidates:
        if os.sep in candidate:
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                return candidate
        elif found := shutil.which(candidate):
            return found
        if required:
            raise SystemExit(f"error: clang-format executable not found: {candidate}")

    raise SystemExit(
        "error: clang-format not found; install it, set CLANG_FORMAT, or pass --clang-format"
    )


def iter_cpp_files(root: Path):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            name
            for name in dirnames
            if name not in PRUNED_DIR_NAMES
            and not name.startswith(PRUNED_DIR_PREFIXES)
        )
        for name in sorted(filenames):
            if Path(name).suffix in CPP_SUFFIXES:
                yield Path(dirpath) / name


def collect_files(paths: list[str]) -> list[Path]:
    files: list[Path] = []
    for raw in paths:
        path = Path(raw)
        if not path.exists():
            raise SystemExit(f"error: no such file or directory: {raw}")
        if path.is_dir():
            files.extend(iter_cpp_files(path))
        else:
            files.append(path)

    # De-duplicate while keeping a stable order.
    return list(dict.fromkeys(path.resolve() for path in files))


def run_clang_format(clang_format: str, path: Path) -> tuple[bytes, bytes]:
    try:
        original = path.read_bytes()
    except OSError as error:
        raise SystemExit(f"error: cannot read {display(path)}: {error}") from error

    result = subprocess.run(
        [clang_format, "--style=file", "--assume-filename", str(path)],
        input=original,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        message = result.stderr.decode(errors="replace").strip()
        raise SystemExit(f"error: clang-format failed for {display(path)}: {message}")

    return original, result.stdout


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Format the project's C++ files with clang-format.",
    )
    parser.add_argument(
        "paths",
        nargs="*",
        help="files or directories to format (default: the whole repository)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="only report files that need formatting; do not modify them",
    )
    parser.add_argument(
        "--clang-format",
        metavar="PATH",
        help="clang-format executable to use",
    )
    args = parser.parse_args(argv)

    clang_format = find_clang_format(args.clang_format)
    files = collect_files(args.paths or [str(REPO_ROOT)])

    changed: list[Path] = []
    for path in files:
        original, formatted = run_clang_format(clang_format, path)
        if formatted != original:
            changed.append(path)
            if not args.check:
                path.write_bytes(formatted)

    for path in changed:
        print(f"{'would format' if args.check else 'formatted'}: {display(path)}")

    if args.check and changed:
        print(
            f"\n{len(changed)} of {len(files)} file(s) need formatting; "
            "run `make format` to fix them.",
            file=sys.stderr,
        )
        return 1

    action = "would change" if args.check else "formatted"
    print(f"{action} {len(changed)} of {len(files)} file(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
