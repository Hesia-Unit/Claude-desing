# HESIA Codebase Local (Drone + Serveur)

Ce dossier regroupe les sources locales en trois blocs:
- drone_source: code C++/CUDA/TensorRT du drone
- server_source: code C++ du serveur + UI + liboqs vendoree
- drone_transition_source: scripts et squelettes OP-TEE pour la transition Jetson

Objectif:
- centraliser le code source utile
- separer Drone / Serveur / Transition
- fournir une documentation de navigation et d usage

Contenu exclu:
- builds/caches (build, out, .vs, .venv, __pycache__)
- assets lourds (MODEL, videos, frame_logs)
- secrets hors perimetre drone/serveur

Contenu sensible inclus sur demande:
- server_source/keys (cles publiques/privees, certs, artefacts TLS)
- drone_transition_source/allowlist_priv.pem

README inclus:
- README_00_OVERVIEW.md
- README_01_ARBORESCENCE.md
- README_02_FONCTIONNEMENT.md
- README_03_UTILITE_PAR_PARTIE.md
- drone_source/README.md
- server_source/README.md
- drone_transition_source/README.md
