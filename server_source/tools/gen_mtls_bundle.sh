#!/usr/bin/env bash
set -euo pipefail

# Generate CA + server cert + drone client cert for mTLS.
# Output (default): ../keys/{ca.crt,ca.key,server.crt,server.key,drone.crt,drone.key}
#
# Env:
#   HESIA_CERT_DIR   Output directory (default: ../keys)
#   HESIA_TLS_CN     Server certificate CN (default: hesia-server)
#   HESIA_TLS_SAN    Server SAN (default: DNS:localhost,IP:127.0.0.1)
#   HESIA_DRONE_CN   Drone client CN (default: hesia-drone)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${HESIA_CERT_DIR:-${ROOT_DIR}/keys}"

mkdir -p "${OUT_DIR}"

CA_KEY="${OUT_DIR}/ca.key"
CA_CRT="${OUT_DIR}/ca.crt"

SRV_KEY="${OUT_DIR}/server.key"
SRV_CSR="${OUT_DIR}/server.csr"
SRV_CRT="${OUT_DIR}/server.crt"

DRN_KEY="${OUT_DIR}/drone.key"
DRN_CSR="${OUT_DIR}/drone.csr"
DRN_CRT="${OUT_DIR}/drone.crt"

CN="${HESIA_TLS_CN:-hesia-server}"
SAN="${HESIA_TLS_SAN:-DNS:localhost,IP:127.0.0.1}"
DRN_CN="${HESIA_DRONE_CN:-hesia-drone}"

OPENSSL_BIN="${OPENSSL_BIN:-openssl}"

# 1) CA
if [[ ! -f "${CA_KEY}" ]]; then
  "${OPENSSL_BIN}" genrsa -out "${CA_KEY}" 4096
fi

"${OPENSSL_BIN}" req -x509 -new -nodes \
  -key "${CA_KEY}" \
  -sha256 -days 3650 \
  -subj "/CN=HESIA-CA" \
  -out "${CA_CRT}"

# 2) Server key + CSR
"${OPENSSL_BIN}" genrsa -out "${SRV_KEY}" 4096
"${OPENSSL_BIN}" req -new -key "${SRV_KEY}" -subj "/CN=${CN}" -out "${SRV_CSR}"

SRV_EXT="${OUT_DIR}/server_ext.cnf"
cat > "${SRV_EXT}" <<EOF
[v3_req]
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = ${SAN}
EOF

"${OPENSSL_BIN}" x509 -req \
  -in "${SRV_CSR}" \
  -CA "${CA_CRT}" -CAkey "${CA_KEY}" \
  -CAcreateserial \
  -out "${SRV_CRT}" \
  -days 825 -sha256 \
  -extfile "${SRV_EXT}" -extensions v3_req

# 3) Drone key + CSR
"${OPENSSL_BIN}" genrsa -out "${DRN_KEY}" 4096
"${OPENSSL_BIN}" req -new -key "${DRN_KEY}" -subj "/CN=${DRN_CN}" -out "${DRN_CSR}"

DRN_EXT="${OUT_DIR}/drone_ext.cnf"
cat > "${DRN_EXT}" <<EOF
[v3_req]
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth
EOF

"${OPENSSL_BIN}" x509 -req \
  -in "${DRN_CSR}" \
  -CA "${CA_CRT}" -CAkey "${CA_KEY}" \
  -CAcreateserial \
  -out "${DRN_CRT}" \
  -days 825 -sha256 \
  -extfile "${DRN_EXT}" -extensions v3_req

rm -f "${SRV_CSR}" "${DRN_CSR}" "${SRV_EXT}" "${DRN_EXT}"
chmod 600 "${SRV_KEY}" "${DRN_KEY}" "${CA_KEY}" || true

echo "[OK] CA     : ${CA_CRT}"
echo "[OK] Server : ${SRV_CRT} / ${SRV_KEY}"
echo "[OK] Drone  : ${DRN_CRT} / ${DRN_KEY}"
