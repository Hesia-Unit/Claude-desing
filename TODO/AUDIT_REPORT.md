# HESIA-Firmware — Rapport d'Audit de Sécurité Global

**Auditeur** : Claude (Anthropic) — lecture seule, sans accès Jetson physique
**Période** : 2026-04-24
**Portée** : `drone_source/`, `drone_transition_source/` (OP-TEE TA + host), `server_source/`, `tools/`, `security/policies/`, `docs/`, `papers/`, configuration build
**Méthodologie** : analyse statique manuelle + agents de recherche parallèles, lecture doc + code, cross-check entre couches.
**Hors périmètre** : exécution dynamique sur Jetson, fuzzing réel, audit matériel, revue cryptographique formelle (pair reviewed).

---

## 1. Synthèse exécutive

HESIA est un projet **techniquement ambitieux** mêlant :
- Cryptographie post-quantique (ML-KEM-1024, ML-DSA-87) via liboqs.
- OP-TEE Trusted Application avec sealing, attestation P-256, ML-DSA.
- TLS 1.3 mTLS avec SPKI pinning et exporter binding.
- Hardening build (clang CFI, LTO, cloaking, stripping) et runtime (seccomp-bpf, systemd sandboxing).
- Pipeline de perception (YOLO + MiDaS).
- Module Ada "Sentinel" pour surveillance.
- Policy signée Ed25519 root.

**Cependant** : l'audit statique met à jour un ensemble de **faiblesses structurelles** dont 5 sont **critiques (P0)** et nécessitent action immédiate avant mise en production :

| Rang | Faille | Gravité CVSS approx | Résumé |
|------|--------|---------------------|--------|
| 1    | `Faille_04` | 9.1 | Nonce AES-GCM potentiellement rejouable (préfixe 32 bits, reset à zéro) |
| 2    | `Faille_03` | 8.8 | Chemin transport en clair (non-TLS) toujours compilable, bit flip possible |
| 3    | `Faille_02` | 8.7 | Bootstrap OP-TEE TOFU accepté par simple magic constant publique |
| 4    | `Faille_01` | 8.2 | Absence de RPMB sur Jetson SD-boot → rollback complet de l'état TEE |
| 5    | `Faille_05` | 7.8 | HKDF-HMAC-SHA3-512 maison non conforme RFC 5869 / FIPS |

Aggravant : ces 5 P0 **interagissent** (cf. §4 graphe de dépendances) → une chaîne d'exploitation plausible existe pour un attaquant avec root REE.

**Position** : **pas prêt pour un déploiement sensible (défense, OIV, industriel critique)** en l'état. Prêt pour un **prototype de validation** ou un déploiement "dual-use low-criticité" avec mitigations réseau. Les corrections P0+P1 sont évaluées à **3 à 5 mois-homme** au total.

---

## 2. Inventaire des failles identifiées

### 2.1 Priorité P0 — Critique (5)
- `Faille_01_RPMB_absent_rollback_TEE.md`
- `Faille_02_OPTEE_bootstrap_TOFU.md`
- `Faille_03_TLS_fallback_clair.md`
- `Faille_04_AES_GCM_nonce_reuse.md`
- `Faille_05_HKDF_HMAC_SHA3_maison.md`

### 2.2 Priorité P1 — Haute (20)
- `Faille_06_TA_sealing_AES_GCM_sans_AAD.md`
- `Faille_07_pas_de_separation_sender_receiver.md`
- `Faille_08_anti_replay_apres_decryption.md`
- `Faille_09_decrypt_resize_non_borne.md`
- `Faille_10_demo_keys_embarquees.md`
- `Faille_11_TA_wipe_key_internal_bug.md`
- `Faille_12_TA_attest_P256_non_HW_bound.md`
- `Faille_13_TA_recovery_nonce_replay.md`
- `Faille_14_TA_maintenance_cmds_sans_signature.md`
- `Faille_15_TA_sign_attest_digest_accepte_court.md`
- `Faille_16_server_accept_loop_DoS.md`
- `Faille_17_KDF_SP800_108_separateur_ambigu.md`
- `Faille_18_server_allowlist_revoke_sans_signature.md`
- `Faille_19_server_frames_world_readable.md`
- `Faille_20_HESIA_FORENSIC_env_bypass.md`
- `Faille_21_server_rotate_all_keys_shell_injection.md`
- `Faille_22_server_UI_non_authentifiee.md`
- `Faille_23_video_source_path_traversal.md`
- `Faille_24_secrets_non_zeroises_RAM.md`
- `Faille_25_cle_SSH_privee_sur_disque.md`

### 2.3 Priorité P2 — Moyenne (15 regroupées)
- `Faille_GROUPE_P2_hygiene_hardening.md` (items P2-01 à P2-15)

### 2.4 Priorité P3 — Basse (12 regroupées)
- `Faille_GROUPE_P3_observations_mineures.md` (items P3-01 à P3-12)

**Total** : 52 observations documentées.

---

## 3. Cartographie par couche

### 3.1 Cryptographie
- **Post-quantique** : ML-KEM-1024 + ML-DSA-87 via liboqs. Bonne pratique *sur le papier*.
- **Symétrique** : AES-256-GCM (bien), mais construction IV **défaillante** (Faille_04).
- **KDF** : mélange HKDF maison HMAC-SHA3-512 (Faille_05) + SP 800-108 (Faille_17). Politique crypto non unifiée.
- **Hash** : SHA3-512 + SHA-256 utilisés de manière mixte.
- **Séparation sender/receiver** : absente (Faille_07).
- **Zéroisation** : partielle (Faille_24).

### 3.2 Couche TEE (OP-TEE TA)
- **Volume de code** : ta_hesia.c ~97 KB, host main.c ~37 KB. Densité critique élevée.
- **Bootstrap** : TOFU faible (Faille_02).
- **Persistence** : SFS sans RPMB (Faille_01), sealing sans AAD (Faille_06).
- **Attestation** : P-256 non HW-bound (Faille_12).
- **Wipe** : incomplet (Faille_11).
- **Recovery** : nonce rejouable (Faille_13).
- **Commandes admin** : pas de signature offline (Faille_14, Faille_15).

### 3.3 Transport réseau
- **TLS 1.3 mTLS** : bonne architecture, mais path clair compilable (Faille_03).
- **AEAD applicatif** : GCM avec nonce à risque (Faille_04).
- **Anti-replay** : après déchiffrement (Faille_08), borne de trame absente (Faille_09).
- **DoS server** : accept loop vulnérable (Faille_16).

### 3.4 Serveur
- **Handshake** : expose allowlist non signée (Faille_18).
- **Persistance** : frames world-readable (Faille_19).
- **Outils** : scripts shell vulnérables injection (Faille_21).
- **UI** : non authentifiée (Faille_22).
- **Path handling** : traversal potentiel (Faille_23).

### 3.5 Operations / Build
- **Binaire release** : env vars bypass (Faille_20).
- **Clés SSH** : privée sur disque dev (Faille_25).
- **Clés démo** : embarquées (Faille_10).
- **Reproductible builds** : incomplet (P2-05).
- **SBOM** : absent (P2-04).

---

## 4. Graphe de dépendances (chaîne d'attaque plausible)

```
                   ┌────────────────────────┐
                   │  Faille_01 (pas RPMB)  │
                   └──────────┬─────────────┘
                              │ permet rollback
                              ▼
┌─────────────────┐   ┌────────────────────────────────┐
│  Faille_10      │   │  Faille_02 (bootstrap TOFU)    │
│  (demo keys)    │   └──────────┬─────────────────────┘
└────────┬────────┘              │ prend le TA
         │                       ▼
         │              ┌────────────────────────┐
         │              │  Faille_11 (wipe bug)  │──── Faille_06 (sealing)
         │              └──────────┬─────────────┘     │
         │                         │                   ▼
         │                         │          Faille_12 (attest clone)
         │                         ▼
         ▼              ┌──────────────────────┐
┌────────────────┐      │  Faille_14 (maint)   │
│ Faille_03      │      │  Faille_13 (recov)   │
│ (no-TLS path)  │──────┤                      │
└────────┬───────┘      └──────────┬───────────┘
         │                         │
         ▼                         ▼
┌────────────────────────────────────────────┐
│  Faille_04 (nonce reuse)                   │
│  Faille_05 (HKDF maison)                   │  ==> MITM+forge
│  Faille_07 (pas split sender/receiver)     │
│  Faille_08 (replay post-decrypt)           │
│  Faille_09 (no bounds)                     │
└────────────────────┬───────────────────────┘
                     │
                     ▼
        ┌──────────────────────────────┐
        │  Côté serveur exploité :      │
        │  Faille_16 (DoS)              │
        │  Faille_18 (allowlist)        │
        │  Faille_22 (UI non auth)      │
        │  Faille_19 (frames leaks)     │
        └──────────────────────────────┘
```

**Interprétation** : un attaquant avec root REE sur un drone (via compromission d'une lib tierce, ou perte physique du drone) peut :
1. Snapshot `/data/tee/` (Faille_01).
2. Bootstrap le TA avec ses propres secrets (Faille_02).
3. Extraire `attest_priv` via cross-domain sealing (Faille_06) ou attendre un wipe incomplet (Faille_11).
4. Cloner l'identité d'attestation (Faille_12) et se présenter au serveur comme drone légitime.
5. Si TLS downgrade possible (Faille_03) + nonce reuse (Faille_04) → MITM complet.
6. Sur le serveur, exploiter DoS (Faille_16), manipuler allowlist (Faille_18), exfiltrer frames (Faille_19).

---

## 5. Estimation d'effort de remédiation

| Phase | Scope | Durée | Coût approximatif (1 dev sénior) |
|-------|-------|-------|------------------------------------|
| **Phase 1 : P0 critiques** | Faille_01 à 05 | 6 à 10 semaines | ≈ 60 à 90 k€ |
| **Phase 2 : P1 cumulées** | Faille_06 à 25 | 10 à 14 semaines | ≈ 100 à 130 k€ |
| **Phase 3 : P2 hardening** | GROUPE_P2 | 6 à 8 semaines | ≈ 55 à 70 k€ |
| **Phase 4 : P3 hygiène** | GROUPE_P3 | 3 semaines | ≈ 25 k€ |
| **Phase 5 : Validation tiers** | Pentest externe + revue cryptographique formelle | 4 semaines consultants externes | ≈ 60 à 100 k€ |

**Total P0+P1 minimum** : ~4 à 6 mois-homme, ~160 à 220 k€.
**Total complet recommandé (P0 à P3 + pentest)** : ~9 à 12 mois-homme, ~300 à 420 k€.

---

## 6. Recommandations stratégiques

### 6.1 Court terme (sprint 1-2, 4 semaines)
1. **Geler tout déploiement production** tant que les P0 ne sont pas corrigés.
2. **Retirer les clés démo du repo** (Faille_10) : `git rm` + ceremony prod.
3. **Rotater la clé SSH dev** (Faille_25) + passer sur un vault.
4. **Patcher Faille_20** (env vars) : kill switch en 2 jours.
5. **Lancer une revue cryptographique externe** (consultant spécialisé PQC/FIPS) en parallèle.

### 6.2 Moyen terme (3 mois)
1. **Refonte cryptographique** : HKDF d'OpenSSL partout, séparation sender/receiver, borne trames (Faille_04 à 09, 17).
2. **Refonte TEE** : bootstrap signé, wipe exhaustif, fuse-binding attest (Faille_02, 11, 12, 13, 14, 15).
3. **Hardening serveur** : auth UI, allowlist signée, DoS mitigations (Faille_16, 18, 19, 22, 23).
4. **Migration Jetson avec RPMB** (Faille_01) : long terme structurel, commencer évaluation BOM.

### 6.3 Long terme (6-12 mois)
1. **Certification visée** : FIPS 140-3 Level 2 (module crypto), Common Criteria EAL4+ (TA), ANSSI CSPN pour déploiement français.
2. **SBOM + supply chain** : SLSA Level 3, sigstore, reproducible builds.
3. **Red team exercise** avec accès Jetson physique.
4. **Documentation utilisateur** : guide opérateur, playbook incident, politique rotation.

---

## 7. Points positifs de l'audit

Tout n'est pas à refaire. Points solides observés :
- **Architecture de défense en profondeur** bien pensée conceptuellement (multi-couches, policy signée, attestation, binding exporter).
- **Choix cryptographiques post-quantiques** alignés avec l'état de l'art NIST 2024/2025.
- **Systemd hardening** (à vérifier dynamiquement) et seccomp-bpf présents.
- **Build hardening** avec clang CFI, LTO, strip.
- **Séparation des responsabilités** drone/serveur/TA assez propre.
- **Documentation technique interne** abondante (README_00 à 03, SECURITY_HARDENING, HESIA_COMPLETE_REFERENCE).
- **Tests crypto** : KAT partiels présents.
- **Module Ada Sentinel** : choix innovant pour surveillance.

---

## 8. Accès requis pour complément d'audit

Voir `ACCES_JETSON_REQUIS.md` pour la liste détaillée. Résumé :
1. Accès Jetson physique pour valider dynamiquement Faille_01 (rollback), Faille_02 (bootstrap), Faille_12 (attest HW-bound).
2. Access à la pipeline CI/CD pour vérifier reproducible builds.
3. Accès aux procédures de key custody pour évaluer Faille_10 et Faille_25.
4. Entretien avec la sécurité de l'équipe pour la policy de wipe (Faille_11) et recovery (Faille_13).

---

## 9. Conclusion

HESIA est un **prototype techniquement crédible** qui démontre une bonne compréhension des enjeux PQC et TEE, mais dont l'**implémentation concrète présente des défauts structurels** empêchant un déploiement en environnement sensible.

La bonne nouvelle : **aucune faille n'est architecturalement bloquante**. Toutes ont des correctifs connus et documentés. Le travail est substantiel (4 à 6 mois effort dev) mais l'ensemble peut atteindre un niveau de maturité "défense / OIV" avec un investissement de 300 à 500 k€ sur 6 à 12 mois, incluant certification externe.

La question cruciale pour les décideurs est : **l'équipe projet a-t-elle la bande passante et le budget pour cette remédiation complète**, ou faut-il envisager un re-scoping vers un périmètre moins exposé (déploiement civil, non-critique) pendant la durée du hardening ?

Voir `AUDIT_ECONOMIQUE.md` pour l'analyse de viabilité économique et positionnement marché.
