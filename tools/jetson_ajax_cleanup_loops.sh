#!/usr/bin/env bash
set -euo pipefail

IMG="${1:-/mnt/f/Jetson-Ajax/images/source/jetson-sd-full-2026-05-21_135317.img}"
LOG="${2:-/mnt/c/Users/matis/Documents/Hesia-Firmware/artifacts/jetson_ajax/phase2_loop_cleanup.log}"

mkdir -p "$(dirname "$LOG")"

{
  echo "Loop cleanup"
  date -Is
  echo "image=$IMG"
  echo

  echo "dmesg tail:"
  dmesg | tail -80 || true
  echo

  echo "losetup before:"
  losetup -a || true
  echo

  while IFS= read -r line; do
    loopdev="${line%%:*}"
    if [[ -n "$loopdev" ]]; then
      echo "detaching $loopdev"
      losetup -d "$loopdev" || true
    fi
  done < <(losetup -a | grep -F "$IMG" || true)

  echo
  echo "losetup after:"
  losetup -a || true
  echo
  date -Is
} 2>&1 | tee "$LOG"
