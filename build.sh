#!/bin/sh
# NimblePDF top-level build entry point.
# Builds xpdf static library, then the NimblePDF app.
# Pass any arguments through to the underlying `make`s (e.g. `./build.sh clean`).

set -eu

cd "$(dirname "$0")"

echo "==> building xpdf"
cd xpdf
make "$@"
cd ..

echo "==> building NimblePDF app"
cd source
make "$@"
cd ..
