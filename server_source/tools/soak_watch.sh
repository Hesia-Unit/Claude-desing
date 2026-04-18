#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-9000}"
DURATION_SEC="${DURATION_SEC:-600}"
INTERVAL_SEC="${INTERVAL_SEC:-2}"
OUT_DIR="${OUT_DIR:-./bench_out/$(date +%Y%m%d_%H%M%S)}"

PID="${PID:-}"
if [[ -z "${PID}" ]]; then
  PID="$(pgrep -f hesia_server_cpp | head -n1 || true)"
fi

if [[ -z "${PID}" ]]; then
  echo "No server PID found. Set PID=... or start the server first." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
CSV="${OUT_DIR}/metrics.csv"
echo "ts,cpu_pct,rss_kb,conns,log_bytes,audit_bytes" > "$CSV"

echo "soak_watch: PID=${PID} duration=${DURATION_SEC}s interval=${INTERVAL_SEC}s"
end=$(( $(date +%s) + DURATION_SEC ))

while [[ $(date +%s) -le $end ]]; do
  ts="$(date +%s)"
  cpu="$(ps -o %cpu= -p "$PID" | awk '{print $1+0}')"
  rss="$(ps -o rss= -p "$PID" | awk '{print $1+0}')"
  conns="$(ss -ant "sport = :$PORT" 2>/dev/null | tail -n +2 | wc -l | tr -d ' ')"
  log_bytes="$(stat -c %s /var/log/hesia/HESIA-SERVER-CPP.log 2>/dev/null || echo 0)"
  audit_bytes="$(stat -c %s /var/log/hesia/audit.log 2>/dev/null || echo 0)"
  echo "${ts},${cpu},${rss},${conns},${log_bytes},${audit_bytes}" >> "$CSV"
  sleep "$INTERVAL_SEC"
done

echo "Done. Metrics saved to ${CSV}"
