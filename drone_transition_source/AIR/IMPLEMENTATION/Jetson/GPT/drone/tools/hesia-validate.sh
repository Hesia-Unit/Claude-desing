#!/usr/bin/env bash
set -euo pipefail

PASS=0
FAIL=0
WARN=0

ok()   { echo "[OK]   $*"; PASS=$((PASS+1)); }
bad()  { echo "[FAIL] $*"; FAIL=$((FAIL+1)); }
warn() { echo "[WARN] $*"; WARN=$((WARN+1)); }

usage() {
  cat <<'EOF'
Usage: hesia-validate.sh [options]

Options:
  --policy <path>          Path to policy.conf
  --ed-pub <path>          Ed25519 public key (PEM)
  --ed-sig <path>          Ed25519 signature (raw)
  --pqc-pub <path>         PQC public key (raw)
  --pqc-sig <path>         PQC signature (raw)
  --pqc-pub-b64 <path>     PQC public key (base64)
  --pqc-sig-b64 <path>     PQC signature (base64)
  --no-crypto              Skip crypto verification steps
  -h, --help               Show this help

Notes:
  - If raw PQC files are missing but base64 files exist, they will be decoded.
  - Ed25519 verification uses: openssl pkeyutl -verify
  - PQC verification uses liboqs (ML-DSA-87) via a tiny compiled verifier.
EOF
}

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROLE="${HESIA_POLICY_ROLE:-drone}"

find_up_exports() {
  local d="$SCRIPT_DIR"
  for _ in 1 2 3 4 5 6 7; do
    if [ -d "$d/_exports" ]; then
      echo "$d/_exports"
      return 0
    fi
    d=$(dirname "$d")
  done
  return 1
}

EXPORTS_DIR="$(find_up_exports || true)"

if [ -n "${EXPORTS_DIR:-}" ]; then
  POLICY="${EXPORTS_DIR}/policy.conf"
  ED_PUB="${EXPORTS_DIR}/policy_pub.pem"
  ED_SIG="${EXPORTS_DIR}/policy.sig"
  PQC_PUB="${EXPORTS_DIR}/policy_pqc_pk.bin"
  PQC_SIG="${EXPORTS_DIR}/policy.sig.pqc.bin"
  PQC_PUB_B64="${EXPORTS_DIR}/policy_pub.pqc"
  PQC_SIG_B64="${EXPORTS_DIR}/policy.sig.pqc"
else
  POLICY="/etc/hesia/policy/policy.conf"
  ED_PUB="/etc/hesia/policy/policy_pub.pem"
  ED_SIG="/etc/hesia/policy/policy.sig"
  PQC_PUB="/etc/hesia/policy/policy_pqc_pk.bin"
  PQC_SIG="/etc/hesia/policy/policy.sig.pqc.bin"
  PQC_PUB_B64="/etc/hesia/policy/policy_pub.pqc"
  PQC_SIG_B64="/etc/hesia/policy/policy.sig.pqc"
fi
DO_CRYPTO=1

while [ $# -gt 0 ]; do
  case "$1" in
    --policy) POLICY="$2"; shift 2 ;;
    --ed-pub) ED_PUB="$2"; shift 2 ;;
    --ed-sig) ED_SIG="$2"; shift 2 ;;
    --pqc-pub) PQC_PUB="$2"; shift 2 ;;
    --pqc-sig) PQC_SIG="$2"; shift 2 ;;
    --pqc-pub-b64) PQC_PUB_B64="$2"; shift 2 ;;
    --pqc-sig-b64) PQC_SIG_B64="$2"; shift 2 ;;
    --no-crypto) DO_CRYPTO=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1"; usage; exit 1 ;;
  esac
done

tmp_dir=""
cleanup() {
  if [ -n "$tmp_dir" ] && [ -d "$tmp_dir" ]; then
    rm -rf "$tmp_dir"
  fi
}
trap cleanup EXIT

ensure_tmp_dir() {
  if [ -z "$tmp_dir" ]; then
    tmp_dir=$(mktemp -d)
  fi
}

decode_base64_file() {
  local src="$1"
  local dst="$2"
  openssl base64 -d -A -in "$src" -out "$dst"
}

materialize_ed_sig_if_needed() {
  local src="$1"
  if [ ! -f "$src" ]; then
    return 1
  fi

  local size
  size=$(wc -c <"$src" | tr -d ' ')
  if [ "$size" = "64" ]; then
    printf '%s\n' "$src"
    return 0
  fi

  if grep -Eq '^[A-Za-z0-9+/=\r\n]+$' "$src"; then
    ensure_tmp_dir
    local decoded="$tmp_dir/policy.sig.ed25519.bin"
    if decode_base64_file "$src" "$decoded" >/dev/null 2>&1; then
      printf '%s\n' "$decoded"
      return 0
    fi
  fi

  return 1
}

find_liboqs_root() {
  local roots=(
    "${LIBOQS_ROOT:-}"
    "$HOME/.cache/.hesia/deps/liboqs/install"
    "$HOME/.cache/.hesia/deps/liboqs/build"
    "/home/ajax/.cache/.hesia/deps/liboqs/install"
    "/home/ajax/.cache/.hesia/deps/liboqs/build"
    "/usr/local/liboqs"
    "/opt/liboqs"
  )

  local root
  for root in "${roots[@]}"; do
    [ -n "$root" ] || continue
    if [ -f "$root/include/oqs/oqs.h" ] && [ -d "$root/lib" ]; then
      printf '%s\n' "$root"
      return 0
    fi
  done
  return 1
}

conf_get() {
  local key="$1"
  if [ ! -f "$POLICY" ]; then
    return 1
  fi
  awk -F= -v k="$key" -v rk="${ROLE}.${key}" '
    $0 ~ /^[ \t]*#/ { next }
    $1 == k { v=$2 }
    $1 == rk { rv=$2 }
    END {
      if (rv != "") {
        print rv
      } else if (v != "") {
        print v
      }
    }
  ' "$POLICY"
}

bool_is_true() {
  case "${1:-}" in
    1|true|TRUE|yes|YES|on|ON) return 0 ;;
    *) return 1 ;;
  esac
}

bool_is_false() {
  case "${1:-}" in
    0|false|FALSE|no|NO|off|OFF) return 0 ;;
    *) return 1 ;;
  esac
}

expect_bool() {
  local label="$1"
  local value="$2"
  local expected="$3"
  local hint="${4:-}"

  if [ -z "$value" ]; then
    warn "${label} not set"
    return
  fi

  if [ "$expected" = "true" ]; then
    if bool_is_true "$value"; then
      ok "${label}=true"
    else
      warn "${label}=${value}${hint}"
    fi
  else
    if bool_is_false "$value"; then
      ok "${label}=false"
    else
      warn "${label}=${value}${hint}"
    fi
  fi
}

expect_nonzero_u64() {
  local label="$1"
  local value="$2"

  if [ -z "$value" ]; then
    warn "${label} not set"
  elif [[ "$value" =~ ^[0-9]+$ ]] && [ "$value" -gt 0 ]; then
    ok "${label}=${value}"
  else
    warn "${label}=${value} (expected non-zero)"
  fi
}

check_runtime_blob_access() {
  local path="$1"
  local label="$2"

  if [ ! -f "$path" ]; then
    warn "${label} missing: $path"
    return
  fi

  local meta
  meta=$(stat -c '%U:%G:%a' "$path" 2>/dev/null || true)
  if [ "$meta" = "root:hesia:640" ]; then
    ok "${label} permissions root:hesia:640"
  else
    warn "${label} permissions ${meta:-unknown} (expected root:hesia:640 for user-mode drone runtime)"
  fi

  if [ -r "$path" ]; then
    ok "${label} readable by current user"
  else
    warn "${label} not readable by current user $(id -un)"
  fi

  if command -v lsattr >/dev/null 2>&1; then
    local attrs
    attrs=$(lsattr -d "$path" 2>/dev/null | awk '{print $1}')
    if printf '%s' "$attrs" | grep -q 'i'; then
      ok "${label} immutable bit set"
    else
      warn "${label} immutable bit not set"
    fi
  fi
}

check_drone_process_sandbox() {
  local pids
  pids=$(pgrep -x hesia_drone || true)
  if [ -z "$pids" ]; then
    warn "hesia_drone not running; runtime sandbox checks skipped"
    return
  fi

  local found=0
  while read -r pid; do
    [ -n "$pid" ] || continue
    found=1
    local status="/proc/${pid}/status"
    if [ ! -r "$status" ]; then
      warn "Cannot read ${status}"
      continue
    fi

    local seccomp no_new_privs
    seccomp=$(awk '/^Seccomp:/ {print $2}' "$status")
    no_new_privs=$(awk '/^NoNewPrivs:/ {print $2}' "$status")

    if [ "${seccomp:-0}" = "2" ]; then
      ok "hesia_drone pid=${pid} running with seccomp filter mode"
    elif [ -n "${seccomp:-}" ]; then
      bad "hesia_drone pid=${pid} seccomp=${seccomp} (expected 2)"
    else
      warn "hesia_drone pid=${pid} missing Seccomp status"
    fi

    if [ "${no_new_privs:-0}" = "1" ]; then
      ok "hesia_drone pid=${pid} has NoNewPrivs=1"
    elif [ -n "${no_new_privs:-}" ]; then
      bad "hesia_drone pid=${pid} NoNewPrivs=${no_new_privs} (expected 1)"
    else
      warn "hesia_drone pid=${pid} missing NoNewPrivs status"
    fi
  done <<< "$pids"

  if [ "$found" -eq 0 ]; then
    warn "hesia_drone not running; runtime sandbox checks skipped"
  fi
}

echo "=== HESIA validation ==="

if [ -f "$POLICY" ]; then
  ok "policy.conf found: $POLICY"
else
  bad "policy.conf not found: $POLICY"
fi

if [ -f "$POLICY" ]; then
  secure_dir=$(conf_get "secure_dir" || true)
  if [ -n "$secure_dir" ]; then
    if [ "$secure_dir" = "/etc/hesia/secure" ]; then
      ok "secure_dir=/etc/hesia/secure"
    else
      warn "secure_dir is '$secure_dir' (prod expected /etc/hesia/secure)"
    fi
    if [ -d "$secure_dir" ]; then
      secure_meta=$(stat -c '%U:%G:%a' "$secure_dir" 2>/dev/null || true)
      if [ "$secure_meta" = "root:hesia:750" ]; then
        ok "secure_dir permissions root:hesia:750"
      else
        warn "secure_dir permissions ${secure_meta:-unknown} (expected root:hesia:750)"
      fi
      if [ -x "$secure_dir" ] && [ -r "$secure_dir" ]; then
        ok "secure_dir traversable by current user"
      else
        warn "secure_dir not traversable/readable by current user $(id -un)"
      fi
    else
      bad "secure_dir missing: $secure_dir"
    fi
  else
    warn "secure_dir not set in policy"
  fi

  expect_bool "allow_ephemeral_dilithium" "$(conf_get "allow_ephemeral_dilithium" || true)" "false" " (prod should be false)"
  expect_bool "allow_ephemeral_puf" "$(conf_get "allow_ephemeral_puf" || true)" "false" " (prod should be false)"
  expect_bool "require_tee_attestation" "$(conf_get "require_tee_attestation" || true)" "true" " (prod should be true)"
  expect_bool "require_optee_session_auth" "$(conf_get "require_optee_session_auth" || true)" "true" " (prod should be true)"
  expect_bool "require_mldsa_sign_in_tee" "$(conf_get "require_mldsa_sign_in_tee" || true)" "true" " (prod should be true)"
  expect_bool "require_boot_measure" "$(conf_get "require_boot_measure" || true)" "true" " (prod should be true)"

  boot_path=$(conf_get "boot_measure_path" || true)
  boot_sig=$(conf_get "boot_measure_sig_path" || true)
  boot_pub=$(conf_get "boot_measure_pubkey_path" || true)
  if [ -n "$boot_path" ] && [ -f "$boot_path" ]; then
    ok "boot_measure_path exists: $boot_path"
  elif [ -n "$boot_path" ]; then
    bad "boot_measure_path missing: $boot_path"
  fi
  if [ -n "$boot_sig" ] && [ -f "$boot_sig" ]; then
    ok "boot_measure_sig_path exists: $boot_sig"
  elif [ -n "$boot_sig" ]; then
    bad "boot_measure_sig_path missing: $boot_sig"
  fi
  if [ -n "$boot_pub" ] && [ -f "$boot_pub" ]; then
    ok "boot_measure_pubkey_path exists: $boot_pub"
  elif [ -n "$boot_pub" ]; then
    bad "boot_measure_pubkey_path missing: $boot_pub"
  fi

  expect_bool "require_asset_manifest" "$(conf_get "require_asset_manifest" || true)" "true" " (prod should be true when assets are signed separately)"

  asset_path=$(conf_get "asset_manifest_path" || true)
  asset_sig=$(conf_get "asset_manifest_sig_path" || true)
  asset_pub=$(conf_get "asset_manifest_pubkey_path" || true)
  if [ -n "$asset_path" ] && [ -f "$asset_path" ]; then
    ok "asset_manifest_path exists: $asset_path"
  elif [ -n "$asset_path" ]; then
    bad "asset_manifest_path missing: $asset_path"
  fi
  if [ -n "$asset_sig" ] && [ -f "$asset_sig" ]; then
    ok "asset_manifest_sig_path exists: $asset_sig"
  elif [ -n "$asset_sig" ]; then
    bad "asset_manifest_sig_path missing: $asset_sig"
  fi
  if [ -n "$asset_pub" ] && [ -f "$asset_pub" ]; then
    ok "asset_manifest_pubkey_path exists: $asset_pub"
  elif [ -n "$asset_pub" ]; then
    bad "asset_manifest_pubkey_path missing: $asset_pub"
  fi

  expect_bool "require_ab_slots" "$(conf_get "require_ab_slots" || true)" "true" " (prod should be true for atomic A/B updates)"
  expect_bool "require_rpmb_rollback_storage" "$(conf_get "require_rpmb_rollback_storage" || true)" "true" " (prod should be true)"
  expect_nonzero_u64 "firmware_version" "$(conf_get "firmware_version" || true)"
fi

if [ -f /proc/sys/kernel/randomize_va_space ]; then
  aslr=$(cat /proc/sys/kernel/randomize_va_space || true)
  if [ "$aslr" = "2" ]; then
    ok "ASLR system=2"
  else
    warn "ASLR system=$aslr (expected 2)"
  fi
fi

if [ -f /etc/hesia/keys/drone_public.bin ]; then
  size=$(wc -c </etc/hesia/keys/drone_public.bin | tr -d ' ')
  if [ "$size" -eq 2592 ]; then
    ok "Pinned drone_public.bin size=2592"
  else
    warn "Pinned drone_public.bin size=$size (expected 2592)"
  fi
else
  warn "/etc/hesia/keys/drone_public.bin not found (server pinning check skipped)"
fi

if [ -f /etc/hesia/secure/dilithium5_pk.bin ] && [ -f /etc/hesia/keys/drone_public.bin ]; then
  h1=$(sha256sum /etc/hesia/secure/dilithium5_pk.bin | awk '{print $1}')
  h2=$(sha256sum /etc/hesia/keys/drone_public.bin | awk '{print $1}')
  if [ "$h1" = "$h2" ]; then
    ok "Pinned drone key matches /etc/hesia/secure"
  else
    warn "Pinned drone key != /etc/hesia/secure (server/drone mismatch)"
  fi
fi

if [ -f /etc/hesia/secure/drone_tee_attest_pub.bin ]; then
  size=$(wc -c </etc/hesia/secure/drone_tee_attest_pub.bin | tr -d ' ')
  if [ "$size" -eq 2592 ]; then
    ok "TEE attestation ML-DSA public key exported (2592 bytes)"
  else
    warn "TEE attestation ML-DSA public key size=$size (expected 2592)"
  fi
elif [ -f /etc/hesia/secure/tee_attest_p256_pub.bin ]; then
  size=$(wc -c </etc/hesia/secure/tee_attest_p256_pub.bin | tr -d ' ')
  if [ "$size" -eq 65 ]; then
    warn "Legacy P-256 TEE attestation public key exported (65 bytes)"
  else
    warn "Legacy P-256 TEE attestation public key size=$size (expected 65)"
  fi
else
  warn "No exported TEE attestation public key found"
fi

check_runtime_blob_access "/etc/hesia/secure/optee_session_auth.sealed" "OP-TEE session auth sealed blob"
check_runtime_blob_access "/etc/hesia/secure/dilithium5_sk.sealed" "Drone ML-DSA sealed private key"
check_runtime_blob_access "/etc/hesia/secure/hesia_seed.sealed" "PUF sealed seed"

if [ -f /etc/hesia/secure/optee_session_auth.sealed ]; then
  size=$(wc -c </etc/hesia/secure/optee_session_auth.sealed | tr -d ' ')
  if [ "$size" -gt 32 ]; then
    ok "OP-TEE session auth sealed blob provisioned"
  else
    warn "OP-TEE session auth sealed blob size=$size (expected > 32)"
  fi
else
  warn "/etc/hesia/secure/optee_session_auth.sealed not found"
fi

if compgen -G "/dev/mmcblk*rpmb" >/dev/null 2>&1; then
  ok "RPMB device node present"
elif [ -d /sys/bus/mmc_rpmb/devices ] && [ -n "$(ls -A /sys/bus/mmc_rpmb/devices 2>/dev/null)" ]; then
  ok "RPMB exposed via /sys/bus/mmc_rpmb/devices"
elif grep -R -q -E '^[1-9][0-9]*$' /sys/bus/mmc/devices/*/raw_rpmb_size_mult 2>/dev/null; then
  ok "MMC sysfs reports RPMB capability"
elif grep -R -q '^SD$' /sys/bus/mmc/devices/*/type 2>/dev/null; then
  warn "No RPMB available: detected removable SD media instead of eMMC/MMC with RPMB"
else
  warn "No RPMB capability found in /dev or MMC sysfs"
fi

check_drone_process_sandbox

if [ "$DO_CRYPTO" -eq 1 ]; then
  echo "--- crypto checks ---"
  if command -v openssl >/dev/null 2>&1; then
    if [ -f "$ED_PUB" ] && [ -f "$ED_SIG" ] && [ -f "$POLICY" ]; then
      ed_sig_raw=$(materialize_ed_sig_if_needed "$ED_SIG" || true)
      if [ -n "${ed_sig_raw:-}" ] && \
         openssl pkeyutl -verify -rawin -pubin -inkey "$ED_PUB" -sigfile "$ed_sig_raw" -in "$POLICY" >/dev/null 2>&1; then
        ok "Ed25519 policy signature verified"
      else
        warn "Ed25519 verification failed (check signature format or key)"
      fi
    else
      warn "Ed25519 files missing (pub/sig/policy)"
    fi
  else
    warn "openssl not found; Ed25519 verify skipped"
  fi

  if [ ! -f "$PQC_PUB" ] && [ -f "$PQC_PUB_B64" ]; then
    ensure_tmp_dir
    PQC_PUB="$tmp_dir/policy_pqc_pk.bin"
    openssl base64 -d -A -in "$PQC_PUB_B64" -out "$PQC_PUB" || warn "PQC pub base64 decode failed"
  fi
  if [ ! -f "$PQC_SIG" ] && [ -f "$PQC_SIG_B64" ]; then
    ensure_tmp_dir
    PQC_SIG="$tmp_dir/policy.sig.pqc.bin"
    openssl base64 -d -A -in "$PQC_SIG_B64" -out "$PQC_SIG" || warn "PQC sig base64 decode failed"
  fi

  if [ -f "$PQC_PUB" ] && [ -f "$PQC_SIG" ] && [ -f "$POLICY" ]; then
    if command -v g++ >/dev/null 2>&1; then
      ensure_tmp_dir
      cat >"$tmp_dir/pqc_verify.cpp" <<'CPP'
#include <oqs/oqs.h>
#include <fstream>
#include <vector>
#include <iostream>
static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: verify <policy> <pk> <sig>\n";
        return 2;
    }
    const char* alg = "ML-DSA-87";
    OQS_SIG* sig = OQS_SIG_new(alg);
    if (!sig) {
        std::cerr << "OQS_SIG_new failed\n";
        return 2;
    }
    auto policy = read_file(argv[1]);
    auto pk = read_file(argv[2]);
    auto sigbuf = read_file(argv[3]);
    if (pk.size() != sig->length_public_key) {
        std::cerr << "PK size mismatch\n";
        OQS_SIG_free(sig);
        return 2;
    }
    if (OQS_SIG_verify(sig, policy.data(), policy.size(),
                       sigbuf.data(), sigbuf.size(),
                       pk.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        return 1;
    }
    OQS_SIG_free(sig);
    return 0;
}
CPP
      oqs_root=$(find_liboqs_root || true)
      if [ -n "${oqs_root:-}" ] && \
         g++ -O2 -std=c++17 -I"$oqs_root/include" "$tmp_dir/pqc_verify.cpp" -L"$oqs_root/lib" -Wl,-rpath,"$oqs_root/lib" -loqs -lcrypto -o "$tmp_dir/pqc_verify" >/dev/null 2>&1; then
        if "$tmp_dir/pqc_verify" "$POLICY" "$PQC_PUB" "$PQC_SIG" >/dev/null 2>&1; then
          ok "PQC policy signature verified (ML-DSA-87)"
        else
          warn "PQC verification failed"
        fi
      else
        warn "PQC verifier build failed (liboqs missing or not discoverable)"
      fi
    else
      warn "g++ not found; PQC verify skipped"
    fi
  else
    warn "PQC files missing (pk/sig/policy)"
  fi
else
  warn "Crypto checks skipped (--no-crypto)"
fi

echo "=== Summary ==="
echo "Pass: $PASS  Warn: $WARN  Fail: $FAIL"
if [ "$FAIL" -gt 0 ]; then
  exit 1
fi
exit 0
