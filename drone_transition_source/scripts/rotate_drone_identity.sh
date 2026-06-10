#!/usr/bin/env bash
set -euo pipefail
umask 077

usage() {
  cat <<'EOF'
Usage: rotate_drone_identity.sh [secure-dir]

Arguments:
  secure-dir   Drone secure directory (default: /etc/hesia/secure)

Environment:
  HESIA_TA_HOST_TOOL   OP-TEE host utility that accepts the "rotate",
                       "recovery_challenge", "recover_session_auth", and "rotate_session_auth" commands
  HESIA_OPTEE_RECOVERY_KEY
                       Offline P-256 recovery private key used for first provisioning
  HESIA_DRONE_SERVICE  systemd service name to restart (default: hesia-drone)
  HESIA_RUNTIME_GROUP  Group allowed to read TEE-sealed runtime blobs (default: hesia)
EOF
}

if [[ "${1:-}" = "-h" || "${1:-}" = "--help" ]]; then
  usage
  exit 0
fi

SECURE_DIR="${1:-/etc/hesia/secure}"
SERVICE_NAME="${HESIA_DRONE_SERVICE:-hesia-drone}"
TA_HOST_TOOL="${HESIA_TA_HOST_TOOL:-}"
RUNTIME_GROUP="${HESIA_RUNTIME_GROUP:-hesia}"
RECOVERY_KEY="${HESIA_OPTEE_RECOVERY_KEY:-}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BACKUP_DIR="${SECURE_DIR}/rotation-backup-${STAMP}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPORT_ANCHORS_SCRIPT="${SCRIPT_DIR}/export_ta_public_anchors.sh"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TOKEN_TOOL="${REPO_ROOT}/tools/generate_optee_recovery_token.py"

case "$SECURE_DIR" in
  /*) ;;
  *)
    echo "[ERROR] secure-dir must be an absolute path." >&2
    exit 2
    ;;
esac

if [[ ! "$SERVICE_NAME" =~ ^[A-Za-z0-9_.@-]{1,64}$ ]]; then
  echo "[ERROR] Invalid systemd service name: ${SERVICE_NAME}" >&2
  exit 2
fi

if [[ "$(id -u)" -ne 0 ]]; then
  echo "[ERROR] This script must run as root." >&2
  exit 1
fi

if ! getent group "$RUNTIME_GROUP" >/dev/null 2>&1; then
  echo "[ERROR] Runtime group '${RUNTIME_GROUP}' does not exist." >&2
  exit 1
fi

install -d -o root -g "$RUNTIME_GROUP" -m 0750 "$SECURE_DIR"
install -d -o root -g root -m 0700 "$BACKUP_DIR"

clear_immutable_flag() {
  local path="$1"
  if command -v chattr >/dev/null 2>&1 && [ -e "$path" ]; then
    chattr -i "$path" 2>/dev/null || true
  fi
}

backup_and_remove() {
  local path="$1"
  if [[ -e "$path" ]]; then
    clear_immutable_flag "$path"
    cp -a "$path" "$BACKUP_DIR"/
    rm -f "$path"
  fi
}

backup_if_exists() {
  local path="$1"
  if [[ -e "$path" ]]; then
    clear_immutable_flag "$path"
    cp -a "$path" "$BACKUP_DIR"/
  fi
}

generate_secret_file() {
  local path="$1"
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -out "$path" 32
  else
    dd if=/dev/urandom of="$path" bs=32 count=1 status=none
  fi
  chmod 600 "$path"
}

protect_runtime_blob() {
  local path="$1"
  clear_immutable_flag "$path"
  chown root:"$RUNTIME_GROUP" "$path"
  chmod 0640 "$path"
  if command -v chattr >/dev/null 2>&1; then
    chattr +i "$path" 2>/dev/null || true
  fi
}

echo "[INFO] Backing up current drone identity into ${BACKUP_DIR}"
backup_and_remove "${SECURE_DIR}/dilithium5_sk.sealed"
backup_and_remove "${SECURE_DIR}/dilithium5_pk.bin"
backup_and_remove "${SECURE_DIR}/tee_attest_p256_pub.bin"
backup_if_exists "${SECURE_DIR}/optee_session_auth.sealed"

if [[ -n "$TA_HOST_TOOL" && -x "$TA_HOST_TOOL" ]]; then
  auth_tmp="${SECURE_DIR}/optee_session_auth.next.raw"
  auth_sealed="${SECURE_DIR}/optee_session_auth.sealed"
  auth_challenge="${SECURE_DIR}/optee_session_auth.challenge.bin"
  auth_token="${SECURE_DIR}/optee_session_auth.recovery.token"
  rm -f "$auth_tmp"
  generate_secret_file "$auth_tmp"

  if [[ -f "$auth_sealed" ]]; then
    echo "[INFO] Rotating OP-TEE session authentication sealed blob"
    HESIA_OPTEE_SESSION_AUTH_PATH="$auth_sealed" \
      "$TA_HOST_TOOL" rotate_session_auth "$auth_tmp" "$auth_sealed"
  else
    if [[ -z "$RECOVERY_KEY" || ! -f "$RECOVERY_KEY" ]]; then
      echo "[ERROR] HESIA_OPTEE_RECOVERY_KEY must point to the offline recovery private key for first provisioning." >&2
      exit 1
    fi
    if [[ ! -f "$TOKEN_TOOL" ]]; then
      echo "[ERROR] Missing recovery token generator: $TOKEN_TOOL" >&2
      exit 1
    fi
    echo "[INFO] Provisioning OP-TEE session authentication sealed blob through the recovery flow"
    HESIA_OPTEE_SESSION_AUTH_PATH="$auth_sealed" \
      "$TA_HOST_TOOL" recovery_challenge "$auth_challenge"
    python3 "$TOKEN_TOOL" \
      --challenge "$auth_challenge" \
      --secret "$auth_tmp" \
      --key "$RECOVERY_KEY" \
      --out "$auth_token"
    HESIA_OPTEE_SESSION_AUTH_PATH="$auth_sealed" \
      "$TA_HOST_TOOL" recover_session_auth "$auth_token" "$auth_tmp" "$auth_sealed"
  fi
  protect_runtime_blob "$auth_sealed"
  rm -f "$auth_tmp" "$auth_challenge" "$auth_token"
else
  echo "[WARN] HESIA_TA_HOST_TOOL not set; OP-TEE session auth rotation/provisioning skipped." >&2
fi

if [[ -n "$TA_HOST_TOOL" && -x "$TA_HOST_TOOL" ]]; then
  echo "[INFO] Rotating TEE sealing/attestation keys via ${TA_HOST_TOOL}"
  HESIA_OPTEE_SESSION_AUTH_PATH="${SECURE_DIR}/optee_session_auth.sealed" "$TA_HOST_TOOL" rotate
else
  echo "[WARN] HESIA_TA_HOST_TOOL not set; TEE key rotation skipped." >&2
fi

if [[ -n "$TA_HOST_TOOL" && -x "$TA_HOST_TOOL" && -f "$EXPORT_ANCHORS_SCRIPT" ]]; then
  echo "[INFO] Exporting current TA public anchors"
  HESIA_TA_HOST_TOOL="$TA_HOST_TOOL" \
    HESIA_RUNTIME_GROUP="$RUNTIME_GROUP" \
  HESIA_OPTEE_SESSION_AUTH_PATH="${SECURE_DIR}/optee_session_auth.sealed" \
    bash "$EXPORT_ANCHORS_SCRIPT" "$SECURE_DIR"
fi

if command -v systemctl >/dev/null 2>&1; then
  echo "[INFO] Restarting ${SERVICE_NAME}"
  systemctl restart "${SERVICE_NAME}"
fi

for _ in $(seq 1 30); do
  if [[ -f "${SECURE_DIR}/dilithium5_pk.bin" && -f "${SECURE_DIR}/tee_attest_p256_pub.bin" ]]; then
    protect_runtime_blob "${SECURE_DIR}/dilithium5_pk.bin"
    protect_runtime_blob "${SECURE_DIR}/tee_attest_p256_pub.bin"
    echo "[OK] New drone ML-DSA public key and TEE public key exported"
    echo "[NEXT] Copy ${SECURE_DIR}/dilithium5_pk.bin and ${SECURE_DIR}/tee_attest_p256_pub.bin to the server secure_dir."
    exit 0
  fi
  sleep 1
done

echo "[ERROR] Rotation completed but fresh public anchors were not exported in time." >&2
echo "[NEXT] Verify the drone service logs and copy the regenerated public anchors manually." >&2
exit 1
