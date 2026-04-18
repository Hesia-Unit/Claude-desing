#!/usr/bin/env bash
set -euo pipefail

# Usage: ./sync_to_jetson.sh <user@ip> <remote_dir>
TARGET="${1:-}"
REMOTE_DIR="${2:-~/hesia}"

if [[ -z "$TARGET" ]]; then
  echo "Usage: $0 <user@ip> <remote_dir>" >&2
  exit 1
fi

rsync -av --delete --exclude '__pycache__' --exclude '*.o' --exclude 'build' \
  "$(cd "$(dirname "$0")/../.." && pwd)/" "$TARGET:$REMOTE_DIR/"

echo "[OK] Sync terminé: $TARGET:$REMOTE_DIR"
