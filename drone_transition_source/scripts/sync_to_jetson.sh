#!/usr/bin/env bash
set -euo pipefail
umask 077

# Usage: ./sync_to_jetson.sh <user@ip> <remote_dir>
TARGET="${1:-}"
REMOTE_DIR="${2:-~/hesia}"

if [[ -z "$TARGET" ]]; then
  echo "Usage: $0 <user@ip> <remote_dir>" >&2
  exit 1
fi

if [[ ! "$TARGET" =~ ^[A-Za-z0-9._-]+@[A-Za-z0-9._:-]+$ ]]; then
  echo "Invalid SSH target syntax: $TARGET" >&2
  exit 2
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# Sync only the drone runtime and transition material needed on Jetson.
# This avoids leaking server-side secrets, PKI state and local VCS metadata.
rsync -av --delete --protect-args -- \
  --exclude '__pycache__' \
  --exclude '*.o' \
  --exclude 'build' \
  --exclude '.git' \
  --exclude 'server_source' \
  --exclude 'drone_transition_source/allowlist_priv.pem' \
  --exclude 'server_source/keys' \
  --exclude '*.key' \
  --exclude '*.srl' \
  --exclude 'demo_secret.bin' \
  --exclude 'demo_secret.pem' \
  --include 'README_*.md' \
  --include 'SECURITY_HARDENING.md' \
  --include 'drone_source/***' \
  --include 'drone_transition_source/***' \
  --exclude '*' \
  "$REPO_ROOT/" "$TARGET:$REMOTE_DIR/"

echo "[OK] Sync terminé: $TARGET:$REMOTE_DIR"
