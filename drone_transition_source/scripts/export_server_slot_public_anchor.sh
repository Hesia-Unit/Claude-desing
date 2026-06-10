#!/usr/bin/env bash
set -euo pipefail
umask 077

usage() {
  cat <<'EOF'
Usage: export_server_slot_public_anchor.sh [secure-dir]

Arguments:
  secure-dir   Runtime secure directory (default: /etc/hesia/secure)

Environment:
  HESIA_TA_HOST_TOOL            OP-TEE host utility path (required)
  HESIA_RUNTIME_GROUP           Runtime group owning exported anchors (default: hesia)
  HESIA_OPTEE_SESSION_AUTH_PATH Sealed OP-TEE session-auth blob path
EOF
}

if [[ "${1:-}" = "-h" || "${1:-}" = "--help" ]]; then
  usage
  exit 0
fi

SECURE_DIR="${1:-/etc/hesia/secure}"
TA_HOST_TOOL="${HESIA_TA_HOST_TOOL:-}"
RUNTIME_GROUP="${HESIA_RUNTIME_GROUP:-hesia}"
SESSION_AUTH_PATH="${HESIA_OPTEE_SESSION_AUTH_PATH:-${SECURE_DIR}/optee_session_auth.sealed}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "[ERROR] This script must run as root." >&2
  exit 1
fi

if [[ -z "$TA_HOST_TOOL" || ! -x "$TA_HOST_TOOL" ]]; then
  echo "[ERROR] HESIA_TA_HOST_TOOL must point to the OP-TEE host utility." >&2
  exit 1
fi

if ! getent group "$RUNTIME_GROUP" >/dev/null 2>&1; then
  echo "[ERROR] Runtime group '${RUNTIME_GROUP}' does not exist." >&2
  exit 1
fi

install -d -o root -g "$RUNTIME_GROUP" -m 0750 "$SECURE_DIR"

clear_immutable_flag() {
  local path="$1"
  if command -v chattr >/dev/null 2>&1 && [[ -e "$path" ]]; then
    chattr -i "$path" 2>/dev/null || true
  fi
}

protect_exported_file() {
  local path="$1"
  clear_immutable_flag "$path"
  chown root:"$RUNTIME_GROUP" "$path"
  chmod 0640 "$path"
  if command -v chattr >/dev/null 2>&1; then
    chattr +i "$path" 2>/dev/null || true
  fi
}

atomic_install() {
  local src="$1"
  local dest="$2"
  local tmp_dest="${dest}.tmp.$$"
  clear_immutable_flag "$dest"
  install -o root -g "$RUNTIME_GROUP" -m 0640 "$src" "$tmp_dest"
  mv -f "$tmp_dest" "$dest"
  protect_exported_file "$dest"
}

tmp_dir="$(mktemp -d "${SECURE_DIR}/.export-server-anchor.XXXXXX")"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

server_pub_tmp="${tmp_dir}/server_public.bin"

echo "[INFO] Exporting ML-DSA public key from TA server slot"
HESIA_OPTEE_SESSION_AUTH_PATH="$SESSION_AUTH_PATH" \
HESIA_OPTEE_MLDSA_SLOT=server \
  "$TA_HOST_TOOL" export_mldsa_pubkey "$server_pub_tmp"

server_pub_size="$(wc -c <"$server_pub_tmp" | tr -d ' ')"
if [[ "$server_pub_size" != "2592" ]]; then
  echo "[ERROR] Unexpected server ML-DSA public key size: ${server_pub_size}" >&2
  exit 1
fi

atomic_install "$server_pub_tmp" "${SECURE_DIR}/server_public.bin"

echo "[OK] Server slot public anchor exported and installed"
sha256sum "${SECURE_DIR}/server_public.bin"
