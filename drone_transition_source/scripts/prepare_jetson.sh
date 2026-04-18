#!/usr/bin/env bash
set -euo pipefail

# Préparation dépendances minimales (Debian/Ubuntu L4T)
# Ajuste selon ton JetPack.

sudo apt-get update
sudo apt-get install -y \
  build-essential cmake pkg-config \
  libssl-dev python3 python3-venv python3-pip \
  git

# Optionnel: liboqs (selon ta méthode d'installation)
# sudo apt-get install -y liboqs-dev

echo "[OK] Dépendances installées."
echo "Variables utiles:" 
echo "  export HESIA_TLS=1"
echo "  export HESIA_TLS_VERIFY=1"
echo "  export HESIA_TLS_CA_FILE=/chemin/vers/tls_ca.crt"
