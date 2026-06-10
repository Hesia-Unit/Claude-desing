#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export TZ=UTC
export LC_ALL=C
export LANG=C
: "${SOURCE_DATE_EPOCH:=1704067200}"
export SOURCE_DATE_EPOCH

cmake -S "$ROOT_DIR/tests/fuzz" \
      -B "$ROOT_DIR/build/fuzz" \
      -G Ninja \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DHESIA_REPRODUCIBLE_BUILD=ON

cmake --build "$ROOT_DIR/build/fuzz" --parallel
