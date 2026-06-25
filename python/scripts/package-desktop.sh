#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
PY_ROOT="$ROOT/python"
DIST_DIR="$ROOT/dist/hermes-desktop"

if [ ! -f "$ROOT/build/hermes-cli" ]; then
    echo "package-desktop.sh: missing build/hermes-cli; run ./scripts/build.sh first" >&2
    exit 1
fi

if [ ! -f "$ROOT/build/libhermesrelay.dylib" ] && [ ! -f "$ROOT/build/libhermesrelay.so" ]; then
    echo "package-desktop.sh: missing shared library; run ./scripts/build.sh first" >&2
    exit 1
fi

if ! command -v pyinstaller >/dev/null 2>&1; then
    echo "package-desktop.sh: pyinstaller is not installed" >&2
    echo "install it with: pip install '.[packaging]'" >&2
    exit 1
fi

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

PYTHONPATH="$PY_ROOT/src" pyinstaller \
    --noconfirm \
    --onedir \
    --name hermes-desktop \
    --paths "$PY_ROOT/src" \
    --add-data "$PY_ROOT/src/hermes_desktop/assets:hermes_desktop/assets" \
    "$PY_ROOT/src/hermes_desktop/__main__.py"

cp "$ROOT/build/hermes-cli" "$DIST_DIR/" 2>/dev/null || true
cp "$ROOT/build/libhermesrelay.dylib" "$DIST_DIR/" 2>/dev/null || true
cp "$ROOT/build/libhermesrelay.so" "$DIST_DIR/" 2>/dev/null || true

echo "desktop package staged under dist/"
