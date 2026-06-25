#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build"
DIST_DIR="$ROOT/dist"
PKG_NAME="hermes-relay-v1"
PKG_ROOT="$DIST_DIR/$PKG_NAME"
ARCHIVE_PATH="${1:-$DIST_DIR/$PKG_NAME.tar.gz}"

if [ ! -x "$BUILD_DIR/hermes-cli" ] || [ ! -x "$BUILD_DIR/hermes-sim" ]; then
    echo "package.sh: build artifacts missing in $BUILD_DIR" >&2
    exit 1
fi

rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT/bin" "$PKG_ROOT/lib" "$PKG_ROOT/docs" "$PKG_ROOT/packaging" "$PKG_ROOT/python"

cp "$BUILD_DIR/hermes-cli" "$PKG_ROOT/bin/"
cp "$BUILD_DIR/hermes-sim" "$PKG_ROOT/bin/"
find "$BUILD_DIR" -maxdepth 1 \( -name 'libhermesrelay.so' -o -name 'libhermesrelay.dylib' -o -name 'hermesrelay.dll' \) -exec cp {} "$PKG_ROOT/lib/" \;
cp "$ROOT/README.md" "$PKG_ROOT/"
cp -R "$ROOT/docs/." "$PKG_ROOT/docs/"
cp -R "$ROOT/packaging/." "$PKG_ROOT/packaging/"
if [ -d "$ROOT/python" ]; then
    cp -R "$ROOT/python/." "$PKG_ROOT/python/"
fi

mkdir -p "$(dirname "$ARCHIVE_PATH")"
rm -f "$ARCHIVE_PATH"
tar -C "$DIST_DIR" -czf "$ARCHIVE_PATH" "$PKG_NAME"
printf 'package written: %s\n' "$ARCHIVE_PATH"
