#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/mnt/f/Jetson-Ajax/mounts/rootfs}"
BOOT="${2:-/mnt/f/Jetson-Ajax/mounts/boot}"
OUT="${3:-/mnt/c/Users/matis/Documents/Hesia-Firmware/artifacts/jetson_ajax/phase2_rootfs_manifest.log}"

mkdir -p "$(dirname "$OUT")"

{
  echo "Rootfs manifest"
  date -Is
  echo

  echo "=== findmnt ==="
  findmnt "$ROOT" || true
  findmnt "$BOOT" || true
  echo

  echo "=== os-release ==="
  cat "$ROOT/etc/os-release" || true
  echo

  echo "=== nvidia l4t release ==="
  cat "$ROOT/etc/nv_tegra_release" || true
  echo

  echo "=== boot directory ==="
  ls -la "$ROOT/boot" | head -80 || true
  echo

  echo "=== fstab ==="
  cat "$ROOT/etc/fstab" || true
  echo

  echo "=== apt sources ==="
  while IFS= read -r file; do
    echo "--- $file"
    sed -n "1,80p" "$file" || true
  done < <(find "$ROOT/etc/apt" -maxdepth 3 -type f \( -name "*.list" -o -name "*.sources" \) 2>/dev/null | sort)
  echo

  echo "=== opt ==="
  ls -la "$ROOT/opt" || true
  echo

  echo "=== home ==="
  ls -la "$ROOT/home" || true
  echo

  echo "=== boot esp files ==="
  find "$BOOT" -maxdepth 4 -type f 2>/dev/null | sort | head -120 || true
  echo

  echo "=== selected Jetson packages ==="
  if [[ -r "$ROOT/var/lib/dpkg/status" ]]; then
    grep -E "^(Package|Version): (nvidia-l4t|cuda|tensorrt|python3|docker|containerd|mavlink|mavproxy)" "$ROOT/var/lib/dpkg/status" | head -200 || true
  fi
  echo

  date -Is
} > "$OUT" 2>&1

cat "$OUT"
