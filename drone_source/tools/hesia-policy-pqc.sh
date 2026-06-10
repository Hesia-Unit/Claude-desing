#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC="$SCRIPT_DIR/hesia-policy-pqc.cpp"
BIN="$SCRIPT_DIR/hesia-policy-pqc"

build_tool() {
  local liboqs_root="${LIBOQS_ROOT:-$HOME/liboqs}"
  local -a roots=(
    "$liboqs_root"
    "$HOME/.cache/.hesia/deps/liboqs/install"
    "$HOME/.cache/.hesia/deps/liboqs/build"
    "/usr/local/liboqs"
    "/opt/liboqs"
  )

  local include_dir=""
  local lib_dir=""
  for root in "${roots[@]}"; do
    if [ -z "$include_dir" ] && [ -f "$root/include/oqs/oqs.h" ]; then
      include_dir="$root/include"
    fi
    if [ -z "$lib_dir" ] && { [ -f "$root/lib/liboqs.a" ] || [ -f "$root/lib/liboqs.so" ]; }; then
      lib_dir="$root/lib"
    fi
  done

  local -a cxxflags=(-O2 -std=c++17)
  local -a ldflags=(-lcrypto -loqs)
  if [ -n "$include_dir" ]; then
    cxxflags+=("-I$include_dir")
  fi
  if [ -n "$lib_dir" ]; then
    ldflags=("-L$lib_dir" "-Wl,-rpath,$lib_dir" "${ldflags[@]}")
  fi

  g++ "${cxxflags[@]}" "$SRC" "${ldflags[@]}" -o "$BIN"
}

if [ ! -x "$BIN" ] || [ "$SRC" -nt "$BIN" ]; then
  build_tool
fi

exec "$BIN" "$@"
