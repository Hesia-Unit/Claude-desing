#!/usr/bin/env bash
set -euo pipefail

IMG="${1:-/mnt/f/Jetson-Ajax/images/source/jetson-sd-full-2026-05-21_135317.img}"
ROOT="${2:-/mnt/f/Jetson-Ajax/mounts/rootfs}"
BOOT="${3:-/mnt/f/Jetson-Ajax/mounts/boot}"
META="${4:-/mnt/f/Jetson-Ajax/mounts/metadata}"
LOG="${5:-/mnt/c/Users/matis/Documents/Hesia-Firmware/artifacts/jetson_ajax/phase2_mount_attempt_wsl.log}"

mkdir -p "$ROOT" "$BOOT" "$META" "$(dirname "$LOG")"

{
  echo "Phase 2 read-only mount attempt"
  date -Is
  echo "image=$IMG"
  echo "root_mount=$ROOT"
  echo "boot_mount=$BOOT"
  echo "meta=$META"
  echo

  if ! command -v losetup >/dev/null 2>&1; then
    echo "BLOCKER: losetup is not available in WSL"
    exit 20
  fi

  if mountpoint -q "$ROOT"; then
    echo "rootfs already mounted"
  fi
  if mountpoint -q "$BOOT"; then
    echo "boot already mounted"
  fi

  LOOP="$(losetup --find --show --partscan --read-only "$IMG")"
  echo "$LOOP" > "$META/loop_device.txt"
  echo "loop=$LOOP"
  echo

  echo "lsblk:"
  lsblk "$LOOP" || true
  echo

  echo "blkid:"
  blkid "${LOOP}p1" || true
  blkid "${LOOP}p10" || true
  echo

  if ! mountpoint -q "$ROOT"; then
    mount -o ro,noload "${LOOP}p1" "$ROOT"
  fi
  echo "mounted APP=${LOOP}p1 -> $ROOT"

  if ! mountpoint -q "$BOOT"; then
    if mount -o ro "${LOOP}p10" "$BOOT"; then
      echo "mounted ESP=${LOOP}p10 -> $BOOT"
    else
      echo "WARN: ESP boot mount failed"
    fi
  fi

  echo
  echo "rootfs top-level:"
  ls -la "$ROOT" | head -80
  echo

  echo "boot top-level:"
  ls -la "$BOOT" | head -80 || true
  echo

  echo "mount summary:"
  findmnt "$ROOT" || true
  findmnt "$BOOT" || true
  echo

  echo "Phase 2 read-only mount completed"
  date -Is
} 2>&1 | tee "$LOG"
