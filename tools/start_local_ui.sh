#!/usr/bin/env bash
set -euo pipefail

ROOT=/mnt/c/Users/matis/Documents/Hesia-Firmware
LOG_DIR=/var/log/hesia/ui
LOG_FILE="$LOG_DIR/ui-server.out"
PID_FILE=/tmp/hesia_ui.pid

mkdir -p "$LOG_DIR"
pkill -9 -f ui_server.py || true
rm -f "$PID_FILE"

nohup env \
  HESIA_UI_BIND_ADDR=127.0.0.1 \
  HESIA_UI_PORT=8080 \
  HESIA_UI_DATA_DIR=/var/log/hesia/ui \
  HESIA_LOG_FILE=/var/log/hesia/local-server-console.log \
  HESIA_UI_ALLOW_INSECURE_LOCAL=1 \
  HESIA_UI_RATE_LIMIT_WINDOW_SEC=60 \
  HESIA_UI_RATE_LIMIT_MAX_REQUESTS=600 \
  python3 "$ROOT/server_source/tools/ui_server.py" >>"$LOG_FILE" 2>&1 &
echo $! >"$PID_FILE"
sleep 3

echo "PID=$(cat "$PID_FILE")"
ps -p "$(cat "$PID_FILE")" -o pid=,user=,stat=,cmd=
echo "---"
ss -ltnp | grep ':8080 ' || true
echo "---"
tail -n 40 "$LOG_FILE" || true
