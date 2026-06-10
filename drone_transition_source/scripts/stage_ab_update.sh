#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: stage_ab_update.sh --host-tool <path> --slot <A|B> --firmware-version <n> --asset-version <n>

Stages an authenticated A/B update in OP-TEE slot metadata. The actual boot
commit is performed by the firmware on the next successful boot of the staged slot.
EOF
}

HOST_TOOL=""
TARGET_SLOT=""
FIRMWARE_VERSION=""
ASSET_VERSION=""

while [ $# -gt 0 ]; do
  case "$1" in
    --host-tool) HOST_TOOL="$2"; shift 2 ;;
    --slot) TARGET_SLOT="$2"; shift 2 ;;
    --firmware-version) FIRMWARE_VERSION="$2"; shift 2 ;;
    --asset-version) ASSET_VERSION="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [ -z "$HOST_TOOL" ] || [ -z "$TARGET_SLOT" ] || [ -z "$FIRMWARE_VERSION" ] || [ -z "$ASSET_VERSION" ]; then
  usage
  exit 1
fi

if [ ! -x "$HOST_TOOL" ]; then
  echo "Host tool is not executable: $HOST_TOOL" >&2
  exit 1
fi

meta_output="$("$HOST_TOOL" read_slot_meta 2>/dev/null || true)"
if [ -n "$meta_output" ]; then
  active_slot="$(printf '%s\n' "$meta_output" | awk -F= '$1=="active_slot" { print $2 }')"
  pending_slot="$(printf '%s\n' "$meta_output" | awk -F= '$1=="pending_slot" { print $2 }')"

  if [ "$pending_slot" != "0" ] && [ -n "$pending_slot" ]; then
    echo "An update is already pending in OP-TEE slot metadata" >&2
    exit 1
  fi

  case "$TARGET_SLOT" in
    A|a|slot_a|slot-a) desired_slot_id=1 ;;
    B|b|slot_b|slot-b) desired_slot_id=2 ;;
    *) echo "Invalid slot: $TARGET_SLOT" >&2; exit 1 ;;
  esac

  if [ -n "$active_slot" ] && [ "$active_slot" = "$desired_slot_id" ]; then
    echo "Refusing to stage update into currently active slot" >&2
    exit 1
  fi
fi

"$HOST_TOOL" stage_slot_update "$TARGET_SLOT" "$FIRMWARE_VERSION" "$ASSET_VERSION"
echo "Staged slot $TARGET_SLOT with firmware_version=$FIRMWARE_VERSION asset_version=$ASSET_VERSION"
