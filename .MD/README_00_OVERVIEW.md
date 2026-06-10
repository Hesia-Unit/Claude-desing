# HESIA Codebase Local (Drone + Serveur)

Ce dossier regroupe les sources locales en trois blocs:
- drone_source: code C++/CUDA/TensorRT du drone
- server_source: code C++ du serveur + UI + liboqs vendoree
- drone_transition_source: scripts et squelettes OP-TEE pour la transition Jetson
- open_source/hesia-core: bibliotheque open source C ABI pour securite, PQC, watchdog, garde d'inference et bindings multi-langages
- ml: R&D multimodale embarquee, export ONNX, benchmark et simulateur
- papers: rapports techniques et notes de recherche
- docs: manuels d installation, exploitation, architecture et preuves Jetson

Objectif:
- centraliser le code source utile
- separer Drone / Serveur / Transition
- fournir une documentation de navigation et d usage

Contenu exclu:
- builds/caches (build, out, .vs, .venv, __pycache__)
- assets lourds (MODEL, videos, frame_logs)
- secrets hors perimetre drone/serveur

Contenu sensible restant:
- server_source/keys (artefacts publics / non-secret only; les secrets doivent etre provisionnes hors depot)

README inclus:
- README_00_OVERVIEW.md
- README_01_ARBORESCENCE.md
- README_02_FONCTIONNEMENT.md
- README_03_UTILITE_PAR_PARTIE.md
- docs/README.md
- docs/HESIA_ENGINEERING_MANUAL.md
- docs/HESIA_CORELIB_FR.md
- docs/HESIA_CORELIB_EN.md
- docs/HESIA_OPERATIONS_RUNBOOK.md
- docs/HESIA_JETSON_BASELINE_2026-04-20.md
- docs/HESIA_M2B_JETSON_BENCHMARK_2026-04-21.md
- ml/README.md
- drone_source/README.md
- server_source/README.md
- drone_transition_source/README.md
