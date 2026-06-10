#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/repro-drone"

if [ -d "$ROOT_DIR/drone_source" ]; then
  SOURCE_DIR="$ROOT_DIR/drone_source"
elif [ -d "$ROOT_DIR/src/drone" ]; then
  SOURCE_DIR="$ROOT_DIR/src/drone"
else
  echo "[ERROR] Unable to locate drone source directory under $ROOT_DIR" >&2
  exit 1
fi

export TZ=UTC
export LC_ALL=C
export LANG=C
: "${SOURCE_DATE_EPOCH:=1704067200}"
export SOURCE_DATE_EPOCH

: "${LIBOQS_ROOT_DIR:=/home/ajax/.cache/.hesia/deps/liboqs/install}"

rm -rf "$BUILD_DIR"

cmake -S "$SOURCE_DIR" \
      -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DHESIA_REPRODUCIBLE_BUILD=ON \
      -DHESIA_ENABLE_CFI=OFF \
      -DHESIA_ENABLE_SENTINEL=ON \
      -DHESIA_ENABLE_LTO=ON \
      -DHESIA_STRIP_SYMBOLS=ON \
      -DLIBOQS_ROOT_DIR="$LIBOQS_ROOT_DIR"

cmake --build "$BUILD_DIR" --parallel
