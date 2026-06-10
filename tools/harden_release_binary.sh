#!/usr/bin/env bash
set -euo pipefail
umask 077

if [ $# -lt 4 ]; then
  echo "Usage: $0 <binary> <debug_dir> <strip_bin> <objcopy_bin> [label]" >&2
  exit 2
fi

binary="$1"
debug_dir="$2"
strip_bin="$3"
objcopy_bin="$4"
label="${5:-$(basename "$binary")}"
debug_file="${debug_dir}/${label}.debug"

if [ ! -f "$binary" ]; then
  echo "Binary not found: $binary" >&2
  exit 1
fi

mkdir -p "$debug_dir"

"$objcopy_bin" --only-keep-debug -- "$binary" "$debug_file"
"$strip_bin" --strip-all -- "$binary"
"$objcopy_bin" --remove-section=.comment -- "$binary" || true
"$objcopy_bin" --remove-section=.note.gnu.build-id -- "$binary" || true
"$objcopy_bin" --remove-section=.note.ABI-tag -- "$binary" || true
"$objcopy_bin" --add-gnu-debuglink="$debug_file" -- "$binary"

chmod 0750 "$binary"
chmod 0640 "$debug_file"

if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  touch -d "@${SOURCE_DATE_EPOCH}" -- "$binary" "$debug_file" 2>/dev/null || true
fi
