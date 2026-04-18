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

POLICY="${EXPORTS_DIR:-}/policy.conf"
ED_PUB="${EXPORTS_DIR:-}/policy_pub.pem"
ED_SIG="${EXPORTS_DIR:-}/policy.sig"
PQC_PUB="${EXPORTS_DIR:-}/policy_pqc_pk.bin"
PQC_SIG="${EXPORTS_DIR:-}/policy.sig.pqc.bin"
PQC_PUB_B64="${EXPORTS_DIR:-}/policy_pub.pqc"
PQC_SIG_B64="${EXPORTS_DIR:-}/policy.sig.pqc"
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

conf_get() {
  local key="$1"
  if [ ! -f "$POLICY" ]; then
    return 1
  fi
  awk -F= -v k="$key" '
    $0 ~ /^[ \t]*#/ { next }
    $1 == k { v=$2 }
    END { if (v != "") print v }
  ' "$POLICY"
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
  else
    warn "secure_dir not set in policy"
  fi

  ep_dil=$(conf_get "allow_ephemeral_dilithium" || true)
  if [ -n "$ep_dil" ]; then
    if [ "$ep_dil" = "false" ]; then
      ok "allow_ephemeral_dilithium=false"
    else
      warn "allow_ephemeral_dilithium=$ep_dil (prod should be false)"
    fi
  else
    warn "allow_ephemeral_dilithium not set"
  fi

  ep_puf=$(conf_get "allow_ephemeral_puf" || true)
  if [ -n "$ep_puf" ]; then
    if [ "$ep_puf" = "false" ]; then
      ok "allow_ephemeral_puf=false"
    else
      warn "allow_ephemeral_puf=$ep_puf (prod should be false)"
    fi
  else
    warn "allow_ephemeral_puf not set"
  fi
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

if [ "$DO_CRYPTO" -eq 1 ]; then
  echo "--- crypto checks ---"
  if command -v openssl >/dev/null 2>&1; then
    if [ -f "$ED_PUB" ] && [ -f "$ED_SIG" ] && [ -f "$POLICY" ]; then
      if openssl pkeyutl -verify -pubin -inkey "$ED_PUB" -sigfile "$ED_SIG" -in "$POLICY" >/dev/null 2>&1; then
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
    tmp_dir=$(mktemp -d)
    PQC_PUB="$tmp_dir/policy_pqc_pk.bin"
    openssl base64 -d -A -in "$PQC_PUB_B64" -out "$PQC_PUB" || warn "PQC pub base64 decode failed"
  fi
  if [ ! -f "$PQC_SIG" ] && [ -f "$PQC_SIG_B64" ]; then
    if [ -z "$tmp_dir" ]; then tmp_dir=$(mktemp -d); fi
    PQC_SIG="$tmp_dir/policy.sig.pqc.bin"
    openssl base64 -d -A -in "$PQC_SIG_B64" -out "$PQC_SIG" || warn "PQC sig base64 decode failed"
  fi

  if [ -f "$PQC_PUB" ] && [ -f "$PQC_SIG" ] && [ -f "$POLICY" ]; then
    if command -v g++ >/dev/null 2>&1; then
      if [ -z "$tmp_dir" ]; then tmp_dir=$(mktemp -d); fi
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
      if g++ -O2 -std=c++17 "$tmp_dir/pqc_verify.cpp" -loqs -lcrypto -o "$tmp_dir/pqc_verify" >/dev/null 2>&1; then
        if "$tmp_dir/pqc_verify" "$POLICY" "$PQC_PUB" "$PQC_SIG" >/dev/null 2>&1; then
          ok "PQC policy signature verified (ML-DSA-87)"
        else
          warn "PQC verification failed"
        fi
      else
        warn "PQC verifier build failed (liboqs missing?)"
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
