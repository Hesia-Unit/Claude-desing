#!/usr/bin/env bash
set -euo pipefail

ROOT=/mnt/c/Users/matis/Documents/Hesia-Firmware
BUILD_DIR="$ROOT/server_source/build-local"
LOG_DIR=/var/log/hesia
LOG_FILE="$LOG_DIR/local-server-console.log"
PID_FILE=/tmp/hesia_local_server.pid

mkdir -p "$LOG_DIR"
pkill -9 -f "$BUILD_DIR/hesia_server_cpp" || true
rm -f "$PID_FILE"

cd "$BUILD_DIR"
nohup env LD_LIBRARY_PATH=/home/valstrax/liboqs/lib:/usr/local/lib ./hesia_server_cpp >>"$LOG_FILE" 2>&1 &
echo $! >"$PID_FILE"
sleep 3

echo "PID=$(cat "$PID_FILE")"
ps -p "$(cat "$PID_FILE")" -o pid=,user=,stat=,cmd=
echo "---"
ss -ltnp | grep ':9000 ' || true
echo "---"
tail -n 40 "$LOG_FILE" || true
