#!/usr/bin/env bash
set -euo pipefail
umask 077

usage() {
  cat <<'EOF'
Usage: provision_optee_session_auth.sh [secure-dir]

Arguments:
  secure-dir   Drone secure directory (default: /etc/hesia/secure)

Environment:
  HESIA_TA_HOST_TOOL       OP-TEE host utility with recovery_challenge/recover_session_auth
  HESIA_OPTEE_RECOVERY_KEY Offline P-256 recovery private key used to sign recovery tokens
  HESIA_RUNTIME_GROUP      Group allowed to read TEE-sealed runtime blobs (default: hesia)
  PYTHON_BIN               Python interpreter for token generation (default: python3)
EOF
}

if [[ "${1:-}" = "-h" || "${1:-}" = "--help" ]]; then
  usage
  exit 0
fi

SECURE_DIR="${1:-/etc/hesia/secure}"
TA_HOST_TOOL="${HESIA_TA_HOST_TOOL:-}"
RECOVERY_KEY="${HESIA_OPTEE_RECOVERY_KEY:-}"
RUNTIME_GROUP="${HESIA_RUNTIME_GROUP:-hesia}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TOKEN_TOOL="${REPO_ROOT}/tools/generate_optee_recovery_token.py"
SEALED_PATH="${SECURE_DIR}/optee_session_auth.sealed"
TMP_SECRET="${SECURE_DIR}/optee_session_auth.next.raw"
TMP_CHALLENGE="${SECURE_DIR}/optee_session_auth.challenge.bin"
TMP_TOKEN="${SECURE_DIR}/optee_session_auth.recovery.token"

case "$SECURE_DIR" in
  /*) ;;
  *)
    echo "[ERROR] secure-dir must be an absolute path." >&2
    exit 2
    ;;
esac

if [[ "$(id -u)" -ne 0 ]]; then
  echo "[ERROR] This script must run as root." >&2
  exit 1
fi

if [[ -z "$TA_HOST_TOOL" || ! -x "$TA_HOST_TOOL" ]]; then
  echo "[ERROR] HESIA_TA_HOST_TOOL must point to the OP-TEE host utility." >&2
  exit 1
fi

if [[ -z "$RECOVERY_KEY" || ! -f "$RECOVERY_KEY" ]]; then
  echo "[ERROR] HESIA_OPTEE_RECOVERY_KEY must point to the offline recovery private key." >&2
  exit 1
fi

if [[ ! -f "$TOKEN_TOOL" ]]; then
  echo "[ERROR] Missing recovery token generator: $TOKEN_TOOL" >&2
  exit 1
fi

if ! getent group "$RUNTIME_GROUP" >/dev/null 2>&1; then
  echo "[ERROR] Runtime group '${RUNTIME_GROUP}' does not exist." >&2
  exit 1
fi

install -d -o root -g "$RUNTIME_GROUP" -m 0750 "$SECURE_DIR"

if [[ -f "$SEALED_PATH" ]]; then
  echo "[INFO] ${SEALED_PATH} already exists; refusing to overwrite." >&2
  echo "[NEXT] Use rotate_drone_identity.sh if you need an authenticated rotation." >&2
  exit 1
fi

cleanup() {
  rm -f "$TMP_SECRET" "$TMP_CHALLENGE" "$TMP_TOKEN"
}
trap cleanup EXIT

protect_runtime_blob() {
  local path="$1"
  if command -v chattr >/dev/null 2>&1 && [ -e "$path" ]; then
    chattr -i "$path" 2>/dev/null || true
  fi
  chown root:"$RUNTIME_GROUP" "$path"
  chmod 0640 "$path"
  if command -v chattr >/dev/null 2>&1; then
    chattr +i "$path" 2>/dev/null || true
  fi
}

if command -v openssl >/dev/null 2>&1; then
  openssl rand -out "$TMP_SECRET" 32
else
  dd if=/dev/urandom of="$TMP_SECRET" bs=32 count=1 status=none
fi
chmod 600 "$TMP_SECRET"

echo "[INFO] Requesting OP-TEE recovery challenge"
HESIA_OPTEE_SESSION_AUTH_PATH="$SEALED_PATH" \
  "$TA_HOST_TOOL" recovery_challenge "$TMP_CHALLENGE"

echo "[INFO] Signing recovery token offline"
"$PYTHON_BIN" "$TOKEN_TOOL" \
  --challenge "$TMP_CHALLENGE" \
  --secret "$TMP_SECRET" \
  --key "$RECOVERY_KEY" \
  --out "$TMP_TOKEN"
chmod 600 "$TMP_TOKEN"

echo "[INFO] Provisioning OP-TEE session authentication secret through the recovery flow"
HESIA_OPTEE_SESSION_AUTH_PATH="$SEALED_PATH" \
  "$TA_HOST_TOOL" recover_session_auth "$TMP_TOKEN" "$TMP_SECRET" "$SEALED_PATH"
protect_runtime_blob "$SEALED_PATH"

echo "[OK] OP-TEE session authentication sealed blob provisioned at ${SEALED_PATH}"
