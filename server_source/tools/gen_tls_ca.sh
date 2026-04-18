#!/usr/bin/env bash
set -euo pipefail

# Génère un CA local (tls_ca.crt) et signe un certificat serveur (tls_server.crt)
# Sortie: Serveur/keys/tls_ca.crt + Serveur/keys/tls_ca.key + Serveur/keys/tls_server.crt + Serveur/keys/tls_server.key
# Usage:
#   cd GPT/Serveur
#   tools/gen_tls_ca.sh
# Options:
#   HESIA_TLS_CN=localhost
#   HESIA_TLS_SAN="DNS:localhost,IP:127.0.0.1"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
KEYS_DIR="${ROOT_DIR}/keys"

mkdir -p "${KEYS_DIR}"

CA_KEY="${KEYS_DIR}/tls_ca.key"
CA_CRT="${KEYS_DIR}/tls_ca.crt"
CA_SRL="${KEYS_DIR}/tls_ca.srl"

SRV_KEY="${KEYS_DIR}/tls_server.key"
SRV_CSR="${KEYS_DIR}/tls_server.csr"
SRV_CRT="${KEYS_DIR}/tls_server.crt"

CN="${HESIA_TLS_CN:-localhost}"
SAN="${HESIA_TLS_SAN:-DNS:localhost,IP:127.0.0.1}"

# 1) CA
if [[ ! -f "${CA_KEY}" ]]; then
  openssl genrsa -out "${CA_KEY}" 4096
fi

openssl req -x509 -new -nodes \
  -key "${CA_KEY}" \
  -sha256 -days 3650 \
  -subj "/CN=HESIA-CA" \
  -out "${CA_CRT}"

# 2) Server key + CSR
openssl genrsa -out "${SRV_KEY}" 4096
openssl req -new -key "${SRV_KEY}" -subj "/CN=${CN}" -out "${SRV_CSR}"

# 3) Extensions: SAN + usages
EXTFILE="${KEYS_DIR}/tls_server_ext.cnf"
cat > "${EXTFILE}" <<EOF
[v3_req]
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = ${SAN}
EOF

# 4) Sign CSR with CA
openssl x509 -req \
  -in "${SRV_CSR}" \
  -CA "${CA_CRT}" -CAkey "${CA_KEY}" \
  -CAcreateserial \
  -out "${SRV_CRT}" \
  -days 825 -sha256 \
  -extfile "${EXTFILE}" -extensions v3_req

rm -f "${SRV_CSR}" "${EXTFILE}"

echo "[OK] CA  : ${CA_CRT}"
echo "[OK] Cert: ${SRV_CRT}"
echo "\nDrone: export HESIA_TLS_VERIFY=1; export HESIA_TLS_CA_FILE=\"${CA_CRT}\""
