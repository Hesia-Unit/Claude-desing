#!/usr/bin/env bash
set -euo pipefail

KEY_PATH="${HESIA_JETSON_SSH_KEY:-${HOME}/.ssh/hesia_jetson}"
HOST="${HESIA_JETSON_SSH_HOST:-ajax@100.101.152.53}"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <remote-command>" >&2
  echo "       $0 --stdin    # read remote bash script from stdin" >&2
  exit 2
fi

if [ ! -f "$KEY_PATH" ]; then
  echo "Missing SSH private key: $KEY_PATH" >&2
  exit 1
fi

if [[ ! "$HOST" =~ ^[A-Za-z0-9._-]+@[A-Za-z0-9._:-]+$ ]]; then
  echo "Invalid SSH destination: $HOST" >&2
  exit 2
fi

if [ "$1" = "--stdin" ]; then
  shift
  exec ssh -T -i "$KEY_PATH" -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new "$HOST" "bash -se"
fi

exec ssh -i "$KEY_PATH" -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new "$HOST" "$@"
