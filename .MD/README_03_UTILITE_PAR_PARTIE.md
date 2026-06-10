# Utilite Par Partie

## drone_source
Role: runtime embarque drone (securite, reseau, pipeline vision)

Sous-blocs:
- securite runtime: ASLR, stack protection, CFI, seccomp/sandbox
- crypto/protocole: TLS channel, secure channel, serialization, policies
- vision: VideoManager, YOLO, MiDaS, clean pipeline
- trust zone: optee_client + sentinel_bridge
- Sentinel (Ada): module complementaire de surveillance

## server_source
Role: endpoint serveur (session securisee, policy, audit, UI)

Sous-blocs:
- coeur session: hesia_server_session + net_framing
- securite: policy, security_audit, tls_utils
- UI: index.html, app.js, styles.css
- crypto vendor: liboqs (PQC)

## drone_transition_source
Role: outillage de transition/deploiement Jetson

Sous-blocs:
- allowlist et signature de reference
- scripts de preparation/synchronisation
- squelette OP-TEE TA/host
