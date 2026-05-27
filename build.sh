#!/bin/sh
# NimblePDF top-level build entry point.
# Builds the NimblePDF app. Poppler is linked via pkg-config; install
# the poppler25.12_devel package on Haiku before running.
#
# Pass any arguments through to make (e.g. `./build.sh clean`).

set -eu

cd "$(dirname "$0")"

echo "==> building NimblePDF app"
cd source
make "$@"
cd ..
