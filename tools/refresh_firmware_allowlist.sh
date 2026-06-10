#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 3 ]; then
  echo "Usage: $0 <drone_bin> [secure_dir] [signing_key]" >&2
  exit 2
fi

drone_bin="$1"
secure_dir="${2:-/etc/hesia/secure}"
signing_key="${3:-${secure_dir}/allowlist_signing.key}"
allowlist_txt="${secure_dir}/firmware_allowlist.txt"
allowlist_sig="${allowlist_txt}.sig"

if [ ! -f "${drone_bin}" ]; then
  echo "Drone binary not found: ${drone_bin}" >&2
  exit 1
fi

if [ ! -f "${signing_key}" ]; then
  echo "Allowlist signing key not found: ${signing_key}" >&2
  exit 1
fi

fw_hash="$(openssl dgst -sha3-512 "${drone_bin}" | sed 's/^.*= //')"

chattr -i "${allowlist_txt}" 2>/dev/null || true
chattr -i "${allowlist_sig}" 2>/dev/null || true

printf 'sha3-512:%s\n' "${fw_hash}" > "${allowlist_txt}"
openssl pkeyutl -sign -rawin -inkey "${signing_key}" -in "${allowlist_txt}" | base64 -w0 > "${allowlist_sig}"
printf '\n' >> "${allowlist_sig}"

chown root:hesia "${allowlist_txt}" "${allowlist_sig}" 2>/dev/null || true
chmod 0640 "${allowlist_txt}" "${allowlist_sig}" 2>/dev/null || true
chattr +i "${allowlist_txt}" 2>/dev/null || true
chattr +i "${allowlist_sig}" 2>/dev/null || true

echo "[hesia] firmware allowlist refreshed:"
echo "  hash=${fw_hash}"
echo "  allowlist=${allowlist_txt}"
echo "  signature=${allowlist_sig}"
