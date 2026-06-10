# Groupe P3 — Observations mineures et dette technique

## Priorité : **P3 — Basse** · Gravité agrégée : ~2.0 à 3.5

Observations de faible gravité ou de dette technique. À traiter en background, pas bloquant pour la production.

---

## P3-01 — Commentaires TODO / FIXME laissés dans le code
**Localisation** : diffus, notamment `drone_source/hesia_drone.cpp`, `server_source/src/hesia_server_session.cpp`.
**Constat** : ~30 `TODO` / `FIXME` relevés par les agents de scan.
**Correctif** : triage + fermeture systématique ou ouverture d'issue tracker.

---

## P3-02 — Fichiers de docs mentionnant une personne interne (`valstrax`)
**Localisation** : `tools/jetson_ssh.sh`, potentiellement d'autres docs.
**Constat** : l'identité `valstrax` apparaît en dur.
**Impact** : leak mineur (OSINT cible l'employé).
**Correctif** : remplacer par `$USER` ou variable env.

---

## P3-03 — `.gitignore` non exhaustif pour IDEs
**Constat** : `.vscode/`, `.idea/`, `.DS_Store` non tous filtrés.
**Correctif** : compléter `.gitignore`.

---

## P3-04 — Pas de politique de branches documentée
**Constat** : pas de `CONTRIBUTING.md`, pas de `CODEOWNERS`.
**Correctif** : ajouter, définir règles de review obligatoire sur `main`.

---

## P3-05 — Licences tierces non agrégées
**Constat** : liboqs, OpenSSL, MiDaS, YOLO ont des licences différentes. Pas de `THIRD_PARTY_LICENSES.md`.
**Impact** : risque légal si commercialisation.
**Correctif** : générer le fichier agrégé automatiquement.

---

## P3-06 — Documentation obsolète
**Constat** : certains liens dans `README_*.md` pointent vers des fichiers qui n'existent plus ou ont été renommés.
**Correctif** : script `tools/check_doc_links.sh`.

---

## P3-07 — Endianness non portable sur quelques structures
**Localisation** : spots isolés dans `drone_source/*.cpp` utilisant `uint32_t` brut.
**Constat** : code ARM64-only en prod, mais tests pourraient tourner sur x86.
**Correctif** : `htobe32` systématique sur wire.

---

## P3-08 — `std::random_device` utilisé comme source aléatoire dans certains tests
**Constat** : acceptable pour tests, mais certains scripts util l'utilisent pour générer des tokens.
**Impact** : pas crypto-strong, implémentation spécifique libstdc++ peut être faible.
**Correctif** : `RAND_bytes` d'OpenSSL partout.

---

## P3-09 — Pas de `clang-format` / `.editorconfig` cohérent
**Constat** : styles de formatage divergents entre fichiers (tabs vs spaces, indentation).
**Correctif** : `clang-format` en pre-commit.

---

## P3-10 — Strings literals dupliquées (noms de commandes TA)
**Localisation** : TA + host.
**Constat** : `"session_auth_v1"`, `"slot_meta_v1"`, etc. dupliqués entre TA et host.
**Correctif** : header partagé `hesia_ta_protocol.h`.

---

## P3-11 — Manque de métriques observability
**Constat** : peu de métriques Prometheus-style exposées (handshake duration, replay hits, cipher ops/sec).
**Impact** : troubleshooting prod difficile.
**Correctif** : endpoint `/metrics` avec auth.

---

## P3-12 — `LICENSE` du projet non explicite
**Constat** : fichier `LICENSE` à vérifier (non lu durant l'audit).
**Impact** : zone juridique floue.
**Correctif** : licence commerciale explicite avec clause crypto export.

---

## Dépendances
Aucune critique. Ces points peuvent être traités indépendamment.

## Jetson requis
Non.

## Effort estimé
- Chaque item : 0.5 à 2 jours.
- Total groupé : ~3 semaines cumulées.
