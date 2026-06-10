# Groupe P2 — Hygiène code, hardening, durabilité

## Priorité : **P2 — Moyenne** · Gravité agrégée : ~4.0 à 5.5

Ce document regroupe ~15 observations de priorité P2 identifiées pendant l'audit. Chacune prise isolément n'est pas critique mais leur accumulation dégrade la posture de sécurité.

---

## P2-01 — Ouverture de logs verbose en production
**Localisation** : `drone_source/hesia_drone.cpp`, `server_source/src/main.cpp` (macros `LOG_INFO`, `LOG_DEBUG`).
**Constat** : les logs `LOG_INFO` et `LOG_DEBUG` sont compilés en production, avec messages qui peuvent inclure des préfixes de clés (4 premiers octets), des IV complets, des device_id.
**Impact** : leak d'informations pour un attaquant lisant `journalctl` ou un log central.
**Correctif** : gating strict `#ifdef HESIA_PROD` → `LOG_DEBUG` devient no-op.

---

## P2-02 — Absence de Sub-Resource Integrity (SRI) sur `server_source/ui/`
**Localisation** : `server_source/ui/index.html`, `app.js`, dépendances JS tierces.
**Constat** : si des libs JS (Chart.js, etc.) sont chargées, aucune SRI `integrity=sha384-...`.
**Impact** : compromission CDN tiers → XSS sur UI opérateur.
**Correctif** : ajouter SRI + `Content-Security-Policy` avec `script-src 'self' 'sha256-...'`.

---

## P2-03 — Dépendances vendorisées sans suivi CVE (liboqs, OpenSSL)
**Localisation** : `third_party/liboqs/`, `third_party/openssl/` (si vendorisé).
**Constat** : liboqs est vendorée, OpenSSL probablement système mais non figé.
**Impact** : CVE non trackée dans les libs crypto.
**Correctif** : `dependabot` ou équivalent + pipeline `trivy` / `grype` sur chaque build.

---

## P2-04 — Pas de SBOM (Software Bill of Materials) publié
**Localisation** : globalement absent.
**Constat** : aucun `sbom.json` (CycloneDX, SPDX) dans les artefacts release.
**Impact** : non-conformité future (EU CRA, US EO 14028), impossibilité de patch rapide.
**Correctif** : `syft packages dir:. -o cyclonedx-json` dans `tools/build_hesia_cloaked_release.sh`.

---

## P2-05 — `tools/build_hesia_cloaked_release.sh` strip avec drapeaux non reproductibles
**Localisation** : `tools/build_hesia_cloaked_release.sh`, `tools/harden_release_binary.sh`.
**Constat** : le strip utilise `strip --strip-all` + `sstrip` parfois, mais pas de `SOURCE_DATE_EPOCH` ni reproducible-builds flags.
**Impact** : deux builds depuis le même commit produisent des binaires différents → impossible à tracer/auditer.
**Correctif** : `SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) cmake ...` + `-Wl,--build-id=none` + fs timestamps normalisés.

---

## P2-06 — `policy.conf` non versionné sémantiquement
**Localisation** : `security/policies/*.policy.conf`.
**Constat** : les policies n'ont pas de champ `schema_version` strict. Un parseur plus ancien pourrait ignorer silencieusement un nouveau champ.
**Impact** : policy mal interprétée sur un drone en retard de version.
**Correctif** : parseur strict qui refuse les champs inconnus + version majeure explicite.

---

## P2-07 — Scripts tests (`tools/tests/*.sh`) sans isolation
**Localisation** : `tools/tests/`.
**Constat** : scripts de test touchent à `/tmp`, parfois à `/etc`, sans mktemp strict. Tests parallèles peuvent collisionner.
**Impact** : false negatives en CI, test flakes.
**Correctif** : `mktemp -d` systematique, `trap cleanup EXIT`.

---

## P2-08 — `clang CFI` activé mais pas `SafeStack` ni `ShadowCallStack`
**Localisation** : `CMakeLists.txt` drone et serveur.
**Constat** : CFI activé (bon) mais SafeStack et SCS désactivés ou non configurés.
**Impact** : ROP partiel possible si CFI contourné (lookup table empoisonnée).
**Correctif** : `-fsanitize=safe-stack` + `-fsanitize=shadow-call-stack` (ARM64 compatible Jetson).

---

## P2-09 — `FORTIFY_SOURCE=3` non positionné
**Localisation** : `CMakeLists.txt`.
**Constat** : build flags n'incluent pas `-D_FORTIFY_SOURCE=3` (v3 recommandé depuis glibc 2.34).
**Impact** : certains BO détectables par v3 passent non détectés.
**Correctif** : ajouter `-D_FORTIFY_SOURCE=3` aux builds `-O2`.

---

## P2-10 — RELRO partiel au lieu de full
**Localisation** : build scripts.
**Constat** : `-Wl,-z,relro` présent mais `-Wl,-z,now` pas systématique → RELRO partiel.
**Impact** : GOT modifiable post-load.
**Correctif** : `-Wl,-z,relro,-z,now` obligatoire.

---

## P2-11 — Pas de `noexec` / `nosuid` sur /tmp et /var/lib/hesia
**Localisation** : `drone_transition_source/systemd/hesia-drone.service` (à vérifier).
**Constat** : systemd sandboxing partiel (`ProtectSystem=strict` probable) mais `ReadWritePaths=` peut inclure des chemins `exec` autorisés.
**Impact** : attaquant peut dropper un binaire dans `/var/lib/hesia` et l'exécuter.
**Correctif** : `NoExecPaths=/var/lib/hesia`, remount `/tmp` en `noexec`.

---

## P2-12 — `seccomp-bpf` profile trop permissif
**Localisation** : `drone_source/seccomp_profile.cpp` (si présent, à vérifier par l'agent Explore).
**Constat** : la whitelist des syscalls autorise probablement `mmap` avec `PROT_EXEC`, `clone` sans namespace restrictions.
**Impact** : JIT attack, escape via namespace clone.
**Correctif** : deny `PROT_EXEC` via seccomp arg filter, deny `CLONE_NEWUSER` etc.

---

## P2-13 — Pas de `AppArmor` / SELinux profile fourni
**Localisation** : absent du dépôt.
**Constat** : bien que systemd soit configuré, aucun profile AA/SELinux n'est livré.
**Impact** : sandbox incomplète face à un kernel vulnérable.
**Correctif** : fournir un profile AA strict (`/etc/apparmor.d/hesia-drone`) qui limite read/write/net.

---

## P2-14 — Tests unitaires crypto (`tests/`) : couverture partielle
**Localisation** : `tests/` (inspection fine nécessaire).
**Constat** : KAT sur HKDF, AES-GCM présents mais pas sur rotation de clés, pas sur replay window, pas sur TA sealing/unsealing.
**Impact** : régressions crypto non détectées.
**Correctif** : viser 90% de couverture sur paths crypto, tests de mutation.

---

## P2-15 — Absence de fuzzing continu
**Localisation** : pas de `fuzz/` directory observé.
**Constat** : aucun harness libFuzzer / AFL++ sur les parseurs (frames, policy, tokens TA).
**Impact** : bugs exploitables non détectés en amont.
**Correctif** : OSS-Fuzz intégration ou CI nightly fuzz 1h sur parseurs critiques.

---

## Dépendances
- P2-09 à P2-14 interagissent entre eux : ensemble ils forment la "défense en profondeur". Corriger un seul ne suffit pas.

## Jetson requis
Partiel — P2-11, P2-12, P2-13 requièrent validation sur Jetson (`ACCES_JETSON_REQUIS.md` §4, §5).

## Effort estimé
- Chaque item : 1 à 3 jours.
- Total groupé : ~6 à 8 semaines cumulées.
