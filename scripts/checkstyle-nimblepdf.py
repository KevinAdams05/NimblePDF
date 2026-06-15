#!/usr/bin/env python3
"""NimblePDF checkstyle wrapper.

Enforces the NimblePDF coding style (docs/STYLE_GUIDE.md) with a set of
deterministic, dependency-free checks. These run on every invocation and are
the authoritative gate used by the pre-commit hook, CI, and the build scripts.

If a Haiku source checkout with the upstream `checkstyle.py` is available
(via $HAIKU_SRC or a known location) it is noted, but the checks below are
the gate -- the upstream pass never relaxes them.

Checks (STYLE_GUIDE.md section in brackets):
  * line length >= 140 columns                              [1.1]
  * Yoda conditions (constant on the left of ==)            [Appendix A]
  * trailing whitespace                                     [2]
  * `nullptr` literal (use NULL)                            [10]
  * `#pragma once` (use #ifndef CLASS_NAME_H guards)        [15.2]
  * TRUE / FALSE macros (use true / false)                  [11]
  * C++ C-header forms <cstdio> etc. (use <stdio.h>)        [14.2]
  * missing GPL copyright header                            [16]

Usage:
    python3 scripts/checkstyle-nimblepdf.py [paths...]
    (default paths: source/haiku core qt)

Exit code: 0 if clean, 1 if any violation is reported, 2 on usage error.
"""

import argparse
import os
import re
import sys
from pathlib import Path

LINE_LIMIT = 140
DEFAULT_PATHS = ["source/haiku", "core", "qt"]

CANDIDATE_HAIKU_ROOTS = [
    os.environ.get("HAIKU_SRC"),
    "~/Code/Haiku/haiku",
    "~/haiku-build/haiku",
]

# C++ wrappers for C headers that the style guide wants in <foo.h> form [14.2].
CXX_C_HEADERS = (
    "cstdio", "cstdlib", "cstring", "cctype", "cmath", "ctime", "cassert",
    "cerrno", "climits", "cstddef", "cstdint", "cwchar", "cwctype", "clocale",
    "csignal", "csetjmp", "cfloat", "cstdarg",
)

_yoda = re.compile(r"\bif\s*\(\s*(B_OK|NULL|true|false|0|\".*?\")\s*==")
_trailing_ws = re.compile(r"[ \t]+$")
_nullptr = re.compile(r"\bnullptr\b")
_pragma_once = re.compile(r"^\s*#\s*pragma\s+once\b")
_true_false = re.compile(r"\b(TRUE|FALSE)\b")
_cxx_c_header = re.compile(r"#\s*include\s*<(" + "|".join(CXX_C_HEADERS) + r")>")


def find_haiku_checkstyle():
    for root in CANDIDATE_HAIKU_ROOTS:
        if not root:
            continue
        candidate = Path(os.path.expanduser(root)) / "src/tools/checkstyle/checkstyle.py"
        if candidate.is_file():
            return candidate
    return None


def check_file(path):
    """Run the NimblePDF rule checks against one file. Returns violation count."""
    violations = 0
    text = Path(path).read_text(encoding="utf-8", errors="replace")

    def report(lineno, message):
        nonlocal violations
        print(f"{path}:{lineno}: {message}")
        violations += 1

    # Whole-file: every .cpp/.h must carry the GPL header [16].
    if "GNU General Public License" not in text:
        report(1, "missing GPL copyright header [STYLE_GUIDE 16]")

    for lineno, line in enumerate(text.splitlines(), start=1):
        visible = line.expandtabs(4)
        if len(visible) >= LINE_LIMIT:
            report(lineno, f"line >= {LINE_LIMIT} cols ({len(visible)}) [1.1]")
        if _yoda.search(line):
            report(lineno, "Yoda condition (constant on left of ==) [Appendix A]")
        if _trailing_ws.search(line):
            report(lineno, "trailing whitespace [2]")
        if _nullptr.search(line):
            report(lineno, "use NULL, not nullptr [10]")
        if _pragma_once.search(line):
            report(lineno, "use #ifndef guard, not #pragma once [15.2]")
        if _true_false.search(line):
            report(lineno, "use true/false, not TRUE/FALSE [11]")
        match = _cxx_c_header.search(line)
        if match:
            report(lineno, f"use <{match.group(1)[1:]}.h>, not <{match.group(1)}> [14.2]")

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
    parser.add_argument("paths", nargs="*", default=DEFAULT_PATHS,
        help=f"Files or directories to check (default: {' '.join(DEFAULT_PATHS)})")
    args = parser.parse_args()

    files = walk_sources(args.paths if args.paths else DEFAULT_PATHS)
    if not files:
        print("no source files found", file=sys.stderr)
        return 2

    total = 0
    for f in files:
        total += check_file(f)

    if find_haiku_checkstyle() is None:
        print("note: Haiku checkstyle.py not found; ran NimblePDF checks only "
              "(set HAIKU_SRC for the upstream pass).", file=sys.stderr)

    if total:
        print(f"\n{total} style violation(s).", file=sys.stderr)
        return 1
    print(f"checkstyle clean ({len(files)} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
