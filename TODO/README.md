# TODO - Audit Sécurité & Économique HESIA

Audit réalisé le **2026-04-24** en lecture seule sur le dépôt `Hesia-Firmware/`.
Jetson Orin Nano Super **non accessible** pour cet audit (voir `ACCES_JETSON_REQUIS.md` pour les tests nécessitant la cible).

## Contenu du dossier

| Fichier | Description |
|---|---|
| `AUDIT_REPORT.md` | Rapport d'audit global, synthèse exécutive et priorités consolidées |
| `AUDIT_ECONOMIQUE.md` | Analyse économique : positionnement, go-to-market, coûts, risques business |
| `ACCES_JETSON_REQUIS.md` | Liste des vérifications qui exigent l'accès physique au Jetson |
| `Faille_XX_*.md` | Fiches individuelles par vulnérabilité (XX = numéro de priorité) |
| `Faille_GROUPE_*.md` | Fiches groupées quand plusieurs failles partagent le même fond |

## Conventions de notation

**Priorité (urgence de correction)**
- **P0 — Critique** : bloquant avant tout déploiement opérationnel. À traiter immédiatement.
- **P1 — Haute** : exploitable dans un scénario réaliste, corriger sous 2 semaines.
- **P2 — Moyenne** : défense en profondeur, corriger sous 2 mois.
- **P3 — Basse** : hygiène, code-smell, corriger en cycle de maintenance.

**Gravité (impact)**
Estimation CVSS 3.1 approximative (sans accès au Jetson réel). Les scores sont indicatifs.

## Priorisation synthétique (détail dans AUDIT_REPORT.md)

### Critiques (P0) — bloquant
- Faille_01 : Absence RPMB → rollback complet de l'état TEE
- Faille_02 : Bootstrap OP-TEE session-auth en TOFU non authentifié
- Faille_03 : TLS fallback silencieux en clair (`tls_enabled` mutable)
- Faille_04 : Réutilisation potentielle de nonce AES-GCM (iv_prefix 32-bit)
- Faille_05 : KDF HKDF-HMAC-SHA3-512 maison non conforme RFC 5869

### Hautes (P1) — corriger avant audit externe
- Faille_06 à Faille_15 (voir fiches individuelles)

### Moyennes (P2)
- Faille_16 à Faille_25

### Groupes divers (P3 et améliorations)
- Faille_GROUPE_*

## Recommandations stratégiques

1. **Bloquer le déploiement production** tant que les P0 ne sont pas corrigées. Le TEE sans RPMB n'est pas défendable contre un attaquant root REE.
2. **Commander un pentest externe** (red team) sur une release candidate post-corrections P0/P1.
3. **Rotation obligatoire** de toutes les clés SSH et OP-TEE bootstrap avant tout déploiement client.
4. **Commander un audit cryptographique indépendant** focalisé sur la séparation de domaines clés TX/RX, la dérivation HKDF et la gestion des nonces GCM.
5. **Cf. `AUDIT_ECONOMIQUE.md`** pour les questions de positionnement, certification et viabilité commerciale.
