#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build"
OPENSSL_PREFIX="${OPENSSL_PREFIX:-/opt/homebrew/opt/openssl@3}"

mkdir -p "$BUILD_DIR"

COMMON_CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -fPIC -I$ROOT/include -I$OPENSSL_PREFIX/include"
COMMON_LDFLAGS="-L$OPENSSL_PREFIX/lib -lcrypto"
HARDEN_FLAGS=""
SAN_FLAGS=""
if [ "${HERMES_HARDENING:-1}" = "1" ]; then
    HARDEN_FLAGS="-O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fno-omit-frame-pointer"
fi
if [ "${HERMES_SANITIZE:-0}" = "1" ]; then
    SAN_FLAGS="-fsanitize=address,undefined"
fi

clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/util.c" -o "$BUILD_DIR/util.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/platform_posix.c" -o "$BUILD_DIR/platform_posix.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/api.c" -o "$BUILD_DIR/api.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/log.c" -o "$BUILD_DIR/log.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/identity.c" -o "$BUILD_DIR/identity.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/crypto_openssl.c" -o "$BUILD_DIR/crypto_openssl.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/envelope.c" -o "$BUILD_DIR/envelope.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/store_fs.c" -o "$BUILD_DIR/store_fs.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/bundle.c" -o "$BUILD_DIR/bundle.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/relay.c" -o "$BUILD_DIR/relay.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/sync.c" -o "$BUILD_DIR/sync.o"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS -c "$ROOT/src/transport_tcp.c" -o "$BUILD_DIR/transport_tcp.o"

ar rcs "$BUILD_DIR/libhermesrelay.a" \
    "$BUILD_DIR/util.o" \
    "$BUILD_DIR/platform_posix.o" \
    "$BUILD_DIR/api.o" \
    "$BUILD_DIR/log.o" \
    "$BUILD_DIR/identity.o" \
    "$BUILD_DIR/crypto_openssl.o" \
    "$BUILD_DIR/envelope.o" \
    "$BUILD_DIR/store_fs.o" \
    "$BUILD_DIR/bundle.o" \
    "$BUILD_DIR/relay.o" \
    "$BUILD_DIR/sync.o" \
    "$BUILD_DIR/transport_tcp.o"

SHARED_EXT="so"
SHARED_FLAGS="-shared"
if [ "$(uname -s)" = "Darwin" ]; then
    SHARED_EXT="dylib"
    SHARED_FLAGS="-dynamiclib -install_name @rpath/libhermesrelay.dylib"
fi

clang $SHARED_FLAGS \
    "$BUILD_DIR/util.o" \
    "$BUILD_DIR/platform_posix.o" \
    "$BUILD_DIR/api.o" \
    "$BUILD_DIR/log.o" \
    "$BUILD_DIR/identity.o" \
    "$BUILD_DIR/crypto_openssl.o" \
    "$BUILD_DIR/envelope.o" \
    "$BUILD_DIR/store_fs.o" \
    "$BUILD_DIR/bundle.o" \
    "$BUILD_DIR/relay.o" \
    "$BUILD_DIR/sync.o" \
    "$BUILD_DIR/transport_tcp.o" \
    $COMMON_LDFLAGS $SAN_FLAGS \
    -o "$BUILD_DIR/libhermesrelay.$SHARED_EXT"

clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS "$ROOT/src/cli.c" "$BUILD_DIR/libhermesrelay.a" $COMMON_LDFLAGS $SAN_FLAGS -o "$BUILD_DIR/hermes-cli"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS "$ROOT/src/sim.c" "$BUILD_DIR/libhermesrelay.a" $COMMON_LDFLAGS $SAN_FLAGS -o "$BUILD_DIR/hermes-sim"
clang $COMMON_CFLAGS $HARDEN_FLAGS $SAN_FLAGS "$ROOT/tests/test_main.c" "$BUILD_DIR/libhermesrelay.a" $COMMON_LDFLAGS $SAN_FLAGS -o "$BUILD_DIR/hermes-test"
