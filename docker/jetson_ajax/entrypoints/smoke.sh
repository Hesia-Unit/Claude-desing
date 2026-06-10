#!/usr/bin/env bash
set -euo pipefail

ROOT="${JETSON_ROOTFS:-/jetson_rootfs}"
REPO="${HESIA_REPO:-/hesia}"
WORKSPACE="${JETSON_WORKSPACE:-/workspace}"
OUT_DIR="$WORKSPACE/artifacts/tests"
OUT="$OUT_DIR/jetson_sim_smoke.txt"

mkdir -p "$OUT_DIR"

{
  echo "Jetson-Ajax Docker smoke"
  date -Is
  echo "container_arch=$(uname -m)"
  echo "rootfs=$ROOT"
  echo "repo=$REPO"
  echo "workspace=$WORKSPACE"
  echo

  echo "=== rootfs visibility ==="
  test -f "$ROOT/etc/os-release"
  cat "$ROOT/etc/os-release"
  echo

  echo "=== l4t release ==="
  cat "$ROOT/etc/nv_tegra_release" || true
  echo

  echo "=== rootfs binary architecture ==="
  file "$ROOT/bin/bash" || true
  file "$ROOT/usr/bin/python3" || true
  echo

  echo "=== HESIA repo visibility ==="
  test -d "$REPO/server_source"
  test -d "$REPO/PLFM_RADAR"
  test -f "$REPO/AGENTS.md"
  ls -la "$REPO" | head -40
  echo

  echo "=== qemu aarch64 userland probe ==="
  if command -v qemu-aarch64-static >/dev/null 2>&1; then
    qemu-aarch64-static -L "$ROOT" "$ROOT/usr/bin/dpkg" --print-architecture || true
    qemu-aarch64-static -L "$ROOT" "$ROOT/usr/bin/python3" --version || true
  else
    echo "qemu-aarch64-static unavailable"
  fi
  echo

  echo "=== safety ==="
  echo "rootfs is mounted read-only in compose"
  echo "simulation mode: ${HESIA_SIM_MODE:-unset}"
  echo

  echo "=== hardware mirror ==="
  test -f "${HESIA_CPUINFO_PATH:-/sim_hw/proc/cpuinfo}"
  test -f "${HESIA_UPTIME_PATH:-/sim_hw/proc/uptime}"
  test -f "${HESIA_CLOCK_SOURCE_PATH:-/sim_hw/sys/devices/system/clocksource/clocksource0/current_clocksource}"
  head -20 "${HESIA_CPUINFO_PATH:-/sim_hw/proc/cpuinfo}"
  cat "${HESIA_CLOCK_SOURCE_PATH:-/sim_hw/sys/devices/system/clocksource/clocksource0/current_clocksource}"
  test -f /sim_hw/metadata/desktop_hardware_snapshot.json
  jq '.cpu.status, .voltage.status, .battery.status, .gpu.status' /sim_hw/metadata/desktop_hardware_snapshot.json
  echo

  echo "Jetson-Ajax Docker smoke completed"
  date -Is
} 2>&1 | tee "$OUT"
