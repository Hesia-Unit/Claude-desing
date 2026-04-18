#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-9000}"
REQUESTS="${REQUESTS:-200}"
CONCURRENCY="${CONCURRENCY:-4}"
TIMEOUT_SEC="${TIMEOUT_SEC:-5}"
CA="${CA:-/etc/hesia/certs/ca.crt}"
CERT="${CERT:-/etc/hesia/certs/drone.crt}"
KEY="${KEY:-/etc/hesia/certs/drone.key}"
SNI="${SNI:-$HOST}"

if [[ ! -f "$CA" || ! -f "$CERT" || ! -f "$KEY" ]]; then
  echo "Missing cert/key. Set CA, CERT, KEY env vars." >&2
  echo "CA=$CA" >&2
  echo "CERT=$CERT" >&2
  echo "KEY=$KEY" >&2
  exit 1
fi

TMP_RESULTS="$(mktemp)"
trap 'rm -f "$TMP_RESULTS"' EXIT

run_once() {
  local out
  out="$(timeout "$TIMEOUT_SEC" openssl s_client \
    -connect "${HOST}:${PORT}" \
    -servername "$SNI" \
    -cert "$CERT" \
    -key "$KEY" \
    -CAfile "$CA" \
    -verify_return_error \
    </dev/null 2>/dev/null || true)"
  if echo "$out" | grep -q "Verify return code: 0 (ok)"; then
    echo ok >>"$TMP_RESULTS"
  else
    echo fail >>"$TMP_RESULTS"
  fi
}

export HOST PORT CA CERT KEY SNI TIMEOUT_SEC TMP_RESULTS
export -f run_once

echo "bench_tls: ${REQUESTS} handshakes, concurrency=${CONCURRENCY}, host=${HOST}:${PORT}"
start_ts=$(date +%s)
seq "$REQUESTS" | xargs -n1 -P "$CONCURRENCY" -I{} bash -lc 'run_once'
end_ts=$(date +%s)

ok=$(grep -c "^ok$" "$TMP_RESULTS" || true)
fail=$(grep -c "^fail$" "$TMP_RESULTS" || true)
elapsed=$((end_ts - start_ts))
[[ $elapsed -lt 1 ]] && elapsed=1
rate=$((REQUESTS / elapsed))

echo "OK=$ok FAIL=$fail ELAPSED=${elapsed}s RATE=${rate} req/s"
