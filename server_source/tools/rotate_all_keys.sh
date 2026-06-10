#!/usr/bin/env bash
set -euo pipefail
umask 077

usage() {
  cat <<'EOF'
Usage: rotate_all_keys.sh [options]

Options:
  --secure-dir <path>   Deployment secure directory for non-TLS secrets/public anchors
  --cert-dir <path>     Deployment certificate directory
  --backup-dir <path>   Backup directory for pre-rotation material
  --skip-mldsa          Do not require server ML-DSA rotation helper
  --skip-drone          Do not call the drone-side rotation helper
  -h, --help            Show this help

Environment hooks:
  OPENSSL_BIN               OpenSSL binary (default: openssl)
  HESIA_SERVER_MLDSA_ROTATOR
    Executable that writes server_secret.bin/server_public.bin or
    mldsa87_secret.bin/mldsa87_public.bin into <secure-dir>.
  HESIA_DRONE_ROTATE_CMD
    Executable called with: <secure-dir>
    Typical value: /usr/local/sbin/rotate_drone_identity.sh
  HESIA_DRONE_SECURE_DIR
    Secure directory path passed to HESIA_DRONE_ROTATE_CMD (default: /etc/hesia/secure)
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"

SECURE_DIR="${HESIA_SECURE_DIR:-${ROOT_DIR}/_rotation/secure}"
CERT_DIR="${HESIA_CERT_DIR:-${ROOT_DIR}/_rotation/certs}"
BACKUP_DIR="${HESIA_BACKUP_DIR:-${ROOT_DIR}/_rotation/backups/${STAMP}}"
OPENSSL_BIN="${OPENSSL_BIN:-openssl}"

REQUIRE_MLDSA=1
RUN_DRONE_ROTATE=1

ensure_absolute_dir() {
  local path="$1"
  case "$path" in
    /*) ;;
    *)
      echo "[ERROR] Path must be absolute: $path" >&2
      exit 2
      ;;
  esac
}

ensure_safe_executable() {
  local path="$1"
  if [[ -z "$path" || ! -x "$path" ]]; then
    echo "[ERROR] Executable not found or not executable: $path" >&2
    exit 2
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --secure-dir) SECURE_DIR="$2"; shift 2 ;;
    --cert-dir) CERT_DIR="$2"; shift 2 ;;
    --backup-dir) BACKUP_DIR="$2"; shift 2 ;;
    --skip-mldsa) REQUIRE_MLDSA=0; shift ;;
    --skip-drone) RUN_DRONE_ROTATE=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

ensure_absolute_dir "$SECURE_DIR"
ensure_absolute_dir "$CERT_DIR"
ensure_absolute_dir "$BACKUP_DIR"

mkdir -p "$SECURE_DIR" "$CERT_DIR" "$BACKUP_DIR"

backup_if_exists() {
  local src="$1"
  if [[ -e "$src" ]]; then
    cp -a "$src" "$BACKUP_DIR"/
  fi
}

generate_ed25519_pair() {
  local priv="$1"
  local pub="$2"
  local label="$3"
  "${OPENSSL_BIN}" genpkey -algorithm Ed25519 -out "$priv"
  "${OPENSSL_BIN}" pkey -in "$priv" -pubout -out "$pub"
  chmod 600 "$priv" || true
  chmod 644 "$pub" || true
  echo "[OK] ${label}: ${pub}"
}

sign_detached_base64() {
  local priv="$1"
  local input="$2"
  local output="$3"
  local sig_tmp="${output}.bin"
  "${OPENSSL_BIN}" pkeyutl -sign -inkey "$priv" -rawin -in "$input" -out "$sig_tmp"
  base64 "$sig_tmp" > "$output"
  rm -f "$sig_tmp"
  chmod 600 "$output" || true
}

echo "[INFO] Backing up existing deployment material into ${BACKUP_DIR}"
backup_if_exists "$SECURE_DIR"
backup_if_exists "$CERT_DIR"

echo "[INFO] Rotating TLS/mTLS material into ${CERT_DIR}"
HESIA_CERT_DIR="$CERT_DIR" OPENSSL_BIN="$OPENSSL_BIN" "${SCRIPT_DIR}/gen_mtls_bundle.sh"

echo "[INFO] Rotating Ed25519 operational keys into ${SECURE_DIR}"
generate_ed25519_pair "${SECURE_DIR}/audit_signing.key" "${SECURE_DIR}/audit_signing.pub" "Audit signing"
generate_ed25519_pair "${SECURE_DIR}/release_signing.key" "${SECURE_DIR}/release_signing.pub" "Release signing"
generate_ed25519_pair "${SECURE_DIR}/allowlist_signing.key" "${SECURE_DIR}/allowlist_signing.pub" "Allowlist signing"

touch "${SECURE_DIR}/firmware_allowlist.txt"
touch "${SECURE_DIR}/boot_measure_allowlist.txt"
touch "${SECURE_DIR}/revoked_drones.txt"
chmod 600 "${SECURE_DIR}/firmware_allowlist.txt" "${SECURE_DIR}/boot_measure_allowlist.txt" "${SECURE_DIR}/revoked_drones.txt" || true
sign_detached_base64 "${SECURE_DIR}/allowlist_signing.key" "${SECURE_DIR}/firmware_allowlist.txt" "${SECURE_DIR}/firmware_allowlist.txt.sig"
sign_detached_base64 "${SECURE_DIR}/allowlist_signing.key" "${SECURE_DIR}/boot_measure_allowlist.txt" "${SECURE_DIR}/boot_measure_allowlist.txt.sig"
sign_detached_base64 "${SECURE_DIR}/allowlist_signing.key" "${SECURE_DIR}/revoked_drones.txt" "${SECURE_DIR}/revoked_drones.txt.sig"

if [[ "$REQUIRE_MLDSA" -eq 1 ]]; then
  if [[ -n "${HESIA_SERVER_MLDSA_ROTATOR:-}" && -x "${HESIA_SERVER_MLDSA_ROTATOR}" ]]; then
    echo "[INFO] Rotating server ML-DSA identity via ${HESIA_SERVER_MLDSA_ROTATOR}"
    ensure_safe_executable "${HESIA_SERVER_MLDSA_ROTATOR}"
    "${HESIA_SERVER_MLDSA_ROTATOR}" "${SECURE_DIR}"
  else
    echo "[ERROR] Server ML-DSA rotation helper missing. Set HESIA_SERVER_MLDSA_ROTATOR or rerun with --skip-mldsa." >&2
    exit 1
  fi

  if [[ ! -f "${SECURE_DIR}/server_secret.bin" && ! -f "${SECURE_DIR}/mldsa87_secret.bin" ]]; then
    echo "[ERROR] ML-DSA rotation helper did not produce a server secret key in ${SECURE_DIR}" >&2
    exit 1
  fi
  if [[ ! -f "${SECURE_DIR}/server_public.bin" && ! -f "${SECURE_DIR}/mldsa87_public.bin" ]]; then
    echo "[ERROR] ML-DSA rotation helper did not produce a server public key in ${SECURE_DIR}" >&2
    exit 1
  fi
fi

if [[ "$RUN_DRONE_ROTATE" -eq 1 ]]; then
  if [[ -n "${HESIA_DRONE_ROTATE_CMD:-}" && -x "${HESIA_DRONE_ROTATE_CMD}" ]]; then
    DRONE_SECURE_DIR="${HESIA_DRONE_SECURE_DIR:-/etc/hesia/secure}"
    echo "[INFO] Triggering drone-side rotation via ${HESIA_DRONE_ROTATE_CMD}"
    ensure_absolute_dir "${DRONE_SECURE_DIR}"
    ensure_safe_executable "${HESIA_DRONE_ROTATE_CMD}"
    "${HESIA_DRONE_ROTATE_CMD}" "${DRONE_SECURE_DIR}"
  else
    echo "[WARN] Drone-side rotation helper missing; rotate the drone identity and TEE keys separately." >&2
  fi
fi

echo "[OK] Rotation staging complete"
echo "[NEXT] Re-sign policy and allowlists with the new signing keys before deployment."
echo "[NEXT] Copy ${CERT_DIR} and ${SECURE_DIR} to the target hosts over a trusted channel."
echo "[NEXT] Deploy updated drone_public.bin and tee_attest_p256_pub.bin to the server secure_dir."
