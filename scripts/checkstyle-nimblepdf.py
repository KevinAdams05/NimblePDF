#!/usr/bin/env python3
"""NimblePDF checkstyle wrapper.

Defers to Haiku's upstream `checkstyle.py` (located in the Haiku source tree
at `src/tools/checkstyle/checkstyle.py`) but applies NimblePDF deviations:

  * Line-length violations are only reported at column 140 or above,
    matching the project's soft warning threshold (see
    docs/STYLE_GUIDE.md §1.1).

Usage:
    python3 scripts/checkstyle-nimblepdf.py [paths...]

Environment:
    HAIKU_SRC   Path to a Haiku source checkout containing checkstyle.py.
                If unset, the wrapper looks in a few common locations
                (~/Code/Haiku/haiku, ~/haiku-build/haiku) and falls back
                to a built-in minimal linter.

Exit code: 0 if clean, 1 if any violation is reported, 2 on usage error.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

LINE_LIMIT = 140

CANDIDATE_HAIKU_ROOTS = [
    os.environ.get("HAIKU_SRC"),
    "~/Code/Haiku/haiku",
    "~/haiku-build/haiku",
]


def find_haiku_checkstyle():
    for root in CANDIDATE_HAIKU_ROOTS:
        if not root:
            continue
        candidate = Path(os.path.expanduser(root)) / "src/tools/checkstyle/checkstyle.py"
        if candidate.is_file():
            return candidate
    return None


def minimal_lint(paths):
    """Built-in fallback: a few cheap regex checks when Haiku's checkstyle
    is unavailable. Not a substitute for the real thing."""
    violations = 0
    yoda = re.compile(r"\bif\s*\(\s*(B_OK|NULL|true|false|0|\".*?\")\s*==")
    trailing_ws = re.compile(r"[ \t]+$")
    tab_then_space = re.compile(r"^\t+ ")
    for path in paths:
        text = Path(path).read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), start=1):
            visible = line.expandtabs(4)
            if len(visible) >= LINE_LIMIT:
                print(f"{path}:{lineno}: line >={LINE_LIMIT} cols ({len(visible)})")
                violations += 1
            if yoda.search(line):
                print(f"{path}:{lineno}: Yoda condition (constant on left)")
                violations += 1
            if trailing_ws.search(line):
                print(f"{path}:{lineno}: trailing whitespace")
                violations += 1
            if tab_then_space.search(line):
                print(f"{path}:{lineno}: tab followed by space (mixed indent)")
                violations += 1
    return violations


def walk_sources(paths):
    out = []
    for p in paths:
        path = Path(p)
        if path.is_dir():
            for ext in (".cpp", ".h", ".cc"):
                out.extend(sorted(path.rglob(f"*{ext}")))
        elif path.is_file():
            out.append(path)
    return [str(p) for p in out]


def main():
    parser = argparse.ArgumentParser(description="NimblePDF checkstyle wrapper")
    parser.add_argument("paths", nargs="*", default=["source/haiku"],
        help="Files or directories to check (default: source/haiku)")
    args = parser.parse_args()

    files = walk_sources(args.paths)
    if not files:
        print("no source files found", file=sys.stderr)
        return 2

    haiku_cs = find_haiku_checkstyle()
    if haiku_cs is None:
        print("warning: Haiku checkstyle.py not found — using built-in minimal linter.\n"
              "         Set HAIKU_SRC to a Haiku checkout for the full check.\n",
              file=sys.stderr)
        return 1 if minimal_lint(files) else 0

    # Upstream checkstyle.py is invoked per-file and emits HTML by default;
    # this stub just forwards arguments. Customize once upstream output is
    # plumbed in.
    rc = 0
    for f in files:
        result = subprocess.run([sys.executable, str(haiku_cs), f])
        rc = max(rc, result.returncode)
    return rc


if __name__ == "__main__":
    sys.exit(main())
