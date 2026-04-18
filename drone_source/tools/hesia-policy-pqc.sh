#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC="$SCRIPT_DIR/hesia-policy-pqc.cpp"
BIN="$SCRIPT_DIR/hesia-policy-pqc"

if [ ! -x "$BIN" ] || [ "$SRC" -nt "$BIN" ]; then
  g++ -O2 -std=c++17 "$SRC" -loqs -lcrypto -o "$BIN"
fi

exec "$BIN" "$@"
