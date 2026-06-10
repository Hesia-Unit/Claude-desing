#!/usr/bin/env bash
set -euo pipefail
umask 077

KEY_PATH="${HESIA_JETSON_SSH_KEY:-${HOME}/.ssh/hesia_jetson}"
HOST_PREFIX="${HESIA_JETSON_SCP_PREFIX:-ajax@100.101.152.53:}"

if [ $# -lt 2 ]; then
  echo "Usage: $0 <source> <dest>" >&2
  echo "Use remote destinations as relative paths prefixed automatically from ajax@100.101.152.53:" >&2
  exit 2
fi

src="$1"
dst="$2"

if [ ! -f "$KEY_PATH" ]; then
  echo "Missing SSH private key: $KEY_PATH" >&2
  exit 1
fi

host_check="${HOST_PREFIX%:}"
if [[ ! "$host_check" =~ ^[A-Za-z0-9._-]+@[A-Za-z0-9._:-]+$ ]]; then
  echo "Invalid SSH destination prefix: $HOST_PREFIX" >&2
  exit 2
fi

if [[ "$src" == remote:* ]]; then
  src="${HOST_PREFIX}${src#remote:}"
fi

if [[ "$dst" == remote:* ]]; then
  dst="${HOST_PREFIX}${dst#remote:}"
fi

exec scp -i "$KEY_PATH" -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new "$src" "$dst"
