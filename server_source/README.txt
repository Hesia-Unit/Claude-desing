HESIA Server (C++) — Serveur TLS1.3 + handshake applicatif Kyber/Dilithium

Objectif
- Remplacer le serveur Python par un serveur C++ aligné sur le drone (mêmes structs, mêmes sérialisations, mêmes KDF/hashes).
- Exiger les composants TLS utilisés pour l’hybridation:
  - TLS 1.3 obligatoire
  - TLS exporter 32B obligatoire (label: "HESIA-EXPORTER-HYBRID-V1")
  - PoP: DRONE_AUTH doit inclure SHA256(DER) du certificat serveur et correspondre exactement
  - Pas de messages “compat” inattendus durant le handshake (pas de KEY_OK)

Build (Linux/WSL)
1) Dépendances:
   - OpenSSL dev (libssl-dev)
   - liboqs + headers (liboqs-dev ou build depuis source)
   - CMake >= 3.16
2) Compilation:
   cd GPT/ServeurCPP
   mkdir -p build && cd build
   cmake ..
   cmake --build . -j

Run
- Par défaut, le serveur charge:
  - TLS cert: ../Serveur/keys/tls_server.crt
  - TLS key : ../Serveur/keys/tls_server.key
  - ML-DSA keys: ../Serveur/keys/demo_secret.bin + demo_public.bin
- Variables d’environnement possibles:
  - HESIA_BIND_ADDR (def: 0.0.0.0)
  - HESIA_PORT (def: 9000)
  - HESIA_TLS_CERT / HESIA_TLS_KEY
  - HESIA_SERVER_KEYS_DIR

Exécution:
  ./hesia_server_cpp
