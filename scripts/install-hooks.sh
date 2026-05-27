#!/bin/sh
# Install NimblePDF git pre-commit hook.
# Re-run safely; this overwrites any existing pre-commit hook.

set -eu

REPO_ROOT="$(git rev-parse --show-toplevel)"
HOOK_SRC="${REPO_ROOT}/scripts/hooks/pre-commit"
HOOK_DST="${REPO_ROOT}/.git/hooks/pre-commit"

if [ ! -f "${HOOK_SRC}" ]; then
    echo "error: ${HOOK_SRC} not found" >&2
    exit 1
fi

if [ ! -d "${REPO_ROOT}/.git" ]; then
    echo "error: not in a git repo (no .git/ at ${REPO_ROOT})" >&2
    exit 1
fi

install -m 0755 "${HOOK_SRC}" "${HOOK_DST}"
echo "installed pre-commit hook → ${HOOK_DST}"
