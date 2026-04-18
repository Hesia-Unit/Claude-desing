#!/usr/bin/env bash
set -euo pipefail

# Génère un certificat auto-signé pour TLS 1.3 (développement / LAN).
# Sortie: SOL/keys/tls_server.crt + SOL/keys/tls_server.key
#
# Usage:
#   ./tools/gen_tls_cert.sh [CN] [SAN]
# Exemple:
#   ./tools/gen_tls_cert.sh hesia-local "DNS:localhost,IP:127.0.0.1"

CN="${1:-hesia-local}"
SAN="${2:-DNS:localhost,IP:127.0.0.1}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KEYS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)/keys"

mkdir -p "${KEYS_DIR}"

KEY_FILE="${KEYS_DIR}/tls_server.key"
CRT_FILE="${KEYS_DIR}/tls_server.crt"

OPENSSL_BIN="${OPENSSL_BIN:-openssl}"

# Config OpenSSL minimal avec SAN
TMP_CFG="$(mktemp)"
cat > "${TMP_CFG}" <<EOF
[req]
distinguished_name=req_distinguished_name
x509_extensions=v3_req
prompt=no

[req_distinguished_name]
CN=${CN}

[v3_req]
subjectAltName=${SAN}
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
EOF

"${OPENSSL_BIN}" req -x509 -newkey rsa:3072 -days 3650 -nodes \
  -keyout "${KEY_FILE}" -out "${CRT_FILE}" \
  -config "${TMP_CFG}"

rm -f "${TMP_CFG}"

chmod 600 "${KEY_FILE}"

echo "OK: ${CRT_FILE}"
echo "OK: ${KEY_FILE}"
