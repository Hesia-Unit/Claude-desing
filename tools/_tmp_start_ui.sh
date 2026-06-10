#!/usr/bin/env bash
set -euo pipefail
pkill -9 -f ui_server.py || true
rm -f /tmp/hesia_ui.pid
cd /mnt/c/Users/matis/Documents/Hesia-Firmware
setsid env \
  PYTHONUNBUFFERED=1 \
  HESIA_UI_BIND_ADDR=127.0.0.1 \
  HESIA_UI_PORT=8080 \
  HESIA_UI_DATA_DIR=/var/log/hesia/ui \
  HESIA_LOG_FILE=/var/log/hesia/local-server-console.log \
  HESIA_UI_ALLOW_INSECURE_LOCAL=1 \
  python3 /mnt/c/Users/matis/Documents/Hesia-Firmware/server_source/tools/ui_server.py \
  >/var/log/hesia/ui/ui-server.out 2>&1 < /dev/null &
echo $! >/tmp/hesia_ui.pid
sleep 3
echo PID=$(cat /tmp/hesia_ui.pid)
ps -p $(cat /tmp/hesia_ui.pid) -o pid=,user=,stat=,cmd=
echo ---
ss -ltnp | grep ':8080 ' || true
echo ---
cat /var/log/hesia/ui/ui-server.out