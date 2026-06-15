#!/usr/bin/env bash
#
# build-hpkg.sh — package NimblePDF into a standalone .hpkg
#
# Runs NATIVELY on a Haiku machine (NimblePDF links against poppler25.12, so
# packaging happens on the same box that built it — typically `shredder`).
#
# Prerequisite: the binary must already be built, i.e.
#     cd source && make
# which produces dist/NimblePDF.
#
# Output: build-pkg/nimblepdf-<version>-<arch>.hpkg
#
# Override the target arch with HAIKU_ARCH (default: x86_64).

set -euo pipefail

ARCH="${HAIKU_ARCH:-x86_64}"
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$PROJ/dist/NimblePDF"
PKG_INFO="$PROJ/package/PackageInfo"
BUILD="$PROJ/build-pkg"
PKG_ROOT="$BUILD/package_root"

if [ ! -x "$BIN" ]; then
	echo "ERROR: $BIN not found — build it first: (cd source && make)" >&2
	exit 1
fi

# ------------------------------------------------------------------
# Stage the package tree
# ------------------------------------------------------------------
rm -rf "$BUILD"
mkdir -p "$PKG_ROOT/apps" \
	"$PKG_ROOT/data/deskbar/menu/Applications" \
	"$PKG_ROOT/data/documentation/packages/nimblepdf"

# App binary. On Haiku `cp` is attribute-aware, so the BEOS:ICON / BEOS:APP_SIG
# attributes (written by the build's mimeset step) come along; re-run mimeset
# to be sure Tracker/Deskbar/About resolve the icon.
cp "$BIN" "$PKG_ROOT/apps/NimblePDF"
chmod +x "$PKG_ROOT/apps/NimblePDF"
mimeset -f "$PKG_ROOT/apps/NimblePDF" || true

# Deskbar leaf (4 levels deep -> package root, then apps/NimblePDF)
ln -sf ../../../../apps/NimblePDF \
	"$PKG_ROOT/data/deskbar/menu/Applications/NimblePDF"

# Documentation
[ -f "$PROJ/LICENSE" ]   && cp "$PROJ/LICENSE"   "$PKG_ROOT/data/documentation/packages/nimblepdf/LICENSE"
[ -f "$PROJ/README.md" ] && cp "$PROJ/README.md" "$PKG_ROOT/data/documentation/packages/nimblepdf/README.md"

cp "$PKG_INFO" "$PKG_ROOT/.PackageInfo"

# ------------------------------------------------------------------
# Build the .hpkg
# ------------------------------------------------------------------
VERSION=$(awk '/^version/ { gsub(/[ \t]+/, " "); print $2 }' "$PKG_INFO")
HPKG="$BUILD/nimblepdf-${VERSION}-${ARCH}.hpkg"

rm -f "$HPKG"
( cd "$PKG_ROOT" && package create -q "$HPKG" )

echo
echo "==> Done"
ls -lh "$HPKG"
