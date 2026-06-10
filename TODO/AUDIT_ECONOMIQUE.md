# HESIA-Firmware — Audit Économique et Viabilité

**Date** : 2026-04-24
**Base documentaire** : `docs/HESIA_COMPLETE_REFERENCE_FR.md`, `docs/HESIA_MISSION_MODEL.md`, `papers/HESIA_M2B_EMBEDDED_MULTIMODAL_POLICY_REPORT_2026-04-20.md`, `README_00_OVERVIEW.md`, `SECURITY_HARDENING.md`, dossier `marketing/` (vide, constat important).

**Portée** : viabilité commerciale, positionnement, coût de mise en conformité, timing marché, risques business.

> ⚠️ Précision méthodologique : cette analyse est menée à partir de **la documentation interne et du code**. Aucun accès à un business plan, à des contrats clients, à des études de marché externes, ni à des chiffres financiers n'a été possible. Les chiffres sont des **ordres de grandeur** pour guider la décision.

---

## 1. Positionnement produit perçu

D'après la documentation technique :
- **Cible produit** : firmware sécurisé embarqué + serveur de contrôle pour drones.
- **Socle matériel** : Jetson Orin Nano Super (vision, IA locale, perception YOLO+MiDaS).
- **Argumentaire crypto** : post-quantique (ML-KEM-1024, ML-DSA-87), TEE OP-TEE, attestation, policy signée, measured boot.
- **Différenciant affiché** : "souveraineté technique" (crypto PQC maison vs. Android/iOS trusted), pipeline clean + hardening complet.

**Interprétation du positionnement** :
- Langage typique du **marché défense / OIV / souveraineté** (références NIST PQC, FIPS, OP-TEE).
- Absence de benchmarks vs. concurrents commerciaux (Parrot ANAFI USA, Elbit, Teal Drones, Skydio Defense).
- Absence de documentation marketing/prix/positionnement cible (`marketing/` vide).
- L'argumentaire post-quantique devance largement la réglementation actuelle → **pari sur un marché 2027-2030**.

---

## 2. Analyse de la maturité produit

| Axe | Maturité observée | TRL estimé |
|-----|-------------------|------------|
| Architecture logicielle | Stable, bien documentée | TRL 5-6 |
| Cryptographie | Implémentée mais avec failles critiques | TRL 4 |
| TEE / OP-TEE | Fonctionnel mais TOFU + rollback | TRL 4 |
| Pipeline perception | YOLO+MiDaS opérationnel | TRL 5 |
| Hardening build/runtime | Partiellement appliqué | TRL 5 |
| Documentation | Abondante, interne | TRL 6 |
| Tests / KAT | Partiel | TRL 4 |
| Validation hardware | Non publique / absente | TRL 3-4 |
| Certification (FIPS, CC, CSPN) | Aucune | TRL 2-3 |
| Marketing / Go-to-market | Inexistant | TRL 1 |

**TRL global estimé** : **4** (composants validés en laboratoire, pas d'intégration représentative prouvée en environnement opérationnel).

Pour le marché défense visé, il faudrait atteindre **TRL 7-8** avant toute commercialisation crédible. L'écart est important.

---

## 3. Coûts de remédiation sécurité (repris de AUDIT_REPORT.md)

| Phase | Durée | Budget (1 sénior 8 k€/mois fully loaded) |
|-------|-------|-------------------------------------------|
| P0 critiques | 2,5 mois | 20 k€ |
| P1 (20 fiches) | 3,5 mois | 28 k€ |
| P2 hardening | 2 mois | 16 k€ |
| P3 hygiène | 0,75 mois | 6 k€ |
| Pentest externe + revue crypto formelle | 1 mois | 60-100 k€ |
| **Total minimum (P0+P1 seuls)** | ~6 mois | **~50 k€ dev + 60 k€ audit = 110 k€** |
| **Total recommandé (tout compris)** | ~9-12 mois | **~80 k€ dev + 100 k€ audit = 180 k€** |

Ces chiffres supposent 1 dev sénior expérimenté crypto embarquée. Un binôme (1 crypto + 1 TEE) accélère mais double le coût à ~360 k€.

---

## 4. Coûts de certification

Pour pénétrer le marché défense/OIV européen :

| Certification | Horizon | Coût ordre grandeur | Durée |
|---------------|---------|----------------------|-------|
| **ANSSI CSPN** (France) | Cible minimale pour marché français | 30 à 80 k€ | 4 à 8 mois |
| **Common Criteria EAL4+** (EU) | Défense EU | 150 à 500 k€ | 12 à 24 mois |
| **FIPS 140-3 Level 2** (crypto module) | US DoD ready | 80 à 200 k€ | 9 à 15 mois |
| **NIAP** (US DoD) | US DoD | 200 à 500 k€ | 12 à 24 mois |
| **EU Radio Equipment Directive** (obligatoire UE 2025+) | UE production | 20 à 50 k€ | 3 à 6 mois |
| **EU Cyber Resilience Act** (2027) | UE IoT/OT | 30 à 100 k€ | variable |

**Budget certification minimum "France/EU défense"** : ~200 à 400 k€.
**Budget certification "Dual-use US/EU défense"** : ~600 k€ à 1,5 M€.

---

## 5. Coûts hardware et BOM

Observation depuis `security/policies/jetson_orin_nano_super_runtime.policy.conf` :
- Plateforme choisie : Jetson Orin Nano Super SD-boot.
- **Problème majeur** : **pas de RPMB** (Faille_01) → anti-rollback impossible nativement.

**Options matérielles** :

| Option | Coût/unité BOM | Impact sécurité |
|--------|----------------|-----------------|
| Jetson Orin Nano Super SD-boot actuel | ~500 $ | P0 rollback non résolu |
| Jetson Orin Nano eMMC (avec RPMB) | ~600 $ | RPMB OK, migration 1 trimestre |
| Jetson Orin NX + SE externe I2C | ~700 $ + 10 $ SE | RPMB OK, + SE sign-only |
| Carte custom (Tegra + TPM 2.0 discret) | ~400 $ BOM | Intégration longue mais contrôlée |

**Migration Jetson eMMC** : ~50 à 150 k€ (NRE + recertification matérielle + supply chain Nvidia), délai 6 à 9 mois.

---

## 6. Modèle économique potentiel

Sans business plan disponible, esquisse basée sur le positionnement technique :

### 6.1 Hypothèse A — Licence logicielle embarquée
- Licence par drone : 2 000 à 10 000 € selon criticité.
- Marché cible : intégrateurs drones défense EU.
- Revenu récurrent : maintenance 20 %/an.
- **Break-even** : 100 à 200 drones/an, soit ~300 à 600 k€ CA/an.
- **Faisabilité** : requiert 3-5 clients intégrateurs. Long cycle de vente (12-24 mois).

### 6.2 Hypothèse B — Service managé (drone as a service)
- HESIA opère le back-office + fournit le firmware signé.
- Revenu par heure de vol ou par drone/mois (200-1000 €/drone/mois).
- **Break-even** : 50-100 drones actifs.
- **Faisabilité** : requiert capacity ops 24/7, SLA défense.

### 6.3 Hypothèse C — Consulting + IP licensing
- HESIA vend son expertise à des donneurs d'ordre (Airbus, Thales, MBDA).
- Projets sur mesure à 100-500 k€.
- IP licensing ~10 % du CA intégrateur.
- **Break-even** : 2-3 contrats/an.
- **Faisabilité** : plus rapide, plus stable, exige une marque forte / références.

### 6.4 Hypothèse D — Dual-use civil (inspection industrielle)
- Abandonner la partie défense, viser l'inspection SNCF/EDF/Total/Enedis.
- Licence 500-2000 €/drone, volume plus élevé.
- **Break-even** : 1 000 drones/an.
- **Faisabilité** : moins de certifications exigées, marché plus direct.

---

## 7. Analyse SWOT

### Forces (Strengths)
- Vision architecturale avancée (PQC + TEE + mTLS) qui anticipe les normes 2027+.
- Documentation technique interne substantielle.
- Choix hardware mainstream (Jetson) favorisant l'accès.
- Module Ada Sentinel = angle différenciant (haute intégrité).
- Intégration CFI/LTO/systemd hardening.

### Faiblesses (Weaknesses)
- **Dette technique crypto** (52 observations d'audit, 5 critiques).
- **Pas de certification** engagée à ce stade.
- **Pas de BOM ni go-to-market** documenté.
- **Équipe apparemment réduite** (traces de `valstrax` hardcodé = signe de petite équipe).
- **Pas de validation client externe** documentée.
- **Jetson SD-boot sans RPMB** = choix hardware qui obère la sécurité.

### Opportunités (Opportunities)
- Marché souveraineté EU post-guerre Ukraine : forte demande sur 2025-2030.
- NIS2 + CRA + Cyber Solidarity Act = contrainte réglementaire → avantage aux solutions sécurisées.
- Tensions géopolitiques (Chine Skydio ban, DJI ban) → place pour des alternatives EU.
- PQC devient obligatoire (NIST PQC transition 2030-2035) : premier mover avantage.
- Financement BPI/EIC/Horizon Europe disponible pour projets de cybersécurité.

### Menaces (Threats)
- **Concurrence** bien établie : Parrot (FR), Elistair (FR), Quantum Systems (DE), Teal (US), Anduril (US).
- **Concurrence indirecte** : kits open source (PX4, ArduPilot) + hardware générique.
- **Cycles de vente défense très longs** (3-5 ans du PoC au premier contrat).
- **Exigences certification** peuvent multiplier budgets par 3-5x.
- **Régulation crypto export** : ITAR/EAR/EU Dual-use (2021/821) → barrières administratives.
- **Risque supply chain** : dépendance Nvidia Jetson (ARM64 + GPU propriétaire).
- **Obsolescence** : l'équipe doit maintenir 7-10 ans (durée de vie défense) → coût récurrent élevé.

---

## 8. Risques business majeurs

### 8.1 Risque #1 — Security debt avant premier contrat
Les failles P0 actuelles rendent le produit **non-déployable sur un cas défense**. Un pentest client révélerait ces failles → annulation du deal. La remédiation consomme 6 à 12 mois et 200-400 k€ avant le premier euro de CA défense.

**Mitigation** : commencer par un déploiement civil (inspection) en parallèle, qui tolère un niveau de sécurité moindre et génère du cash.

### 8.2 Risque #2 — Équipe trop fine
Indices suggérant une équipe de 1 à 3 personnes (identifiants `valstrax` hardcodés, absence de `CODEOWNERS`, style code homogène). Porter HESIA jusqu'à la certification exige **5-10 profils pointus** (crypto, TEE, réseau, driver kernel, ops, commercial défense).

**Mitigation** : levée de fonds seed 1-2 M€, ou partenariat avec un intégrateur pour équipe commune.

### 8.3 Risque #3 — Timing marché PQC
La promesse PQC est réelle mais ne sera exigée commercialement qu'à partir de 2028-2030 (transition NIST). D'ici là, les acheteurs préfèrent des solutions éprouvées ECC/RSA. HESIA est **en avance sur la demande** → risque de "trop tôt".

**Mitigation** : proposer une config hybrid (ECC + ML-KEM) qui satisfait aujourd'hui et demain. Le code actuel semble avoir cette capacité (liboqs présent).

### 8.4 Risque #4 — Dépendance hardware Nvidia
Jetson Orin Nano Super est soumis aux contrôles export US. Une vente à un pays sous sanctions (ou à certaines entités défense EU) peut être bloquée par Nvidia.

**Mitigation** : prévoir un portage sur SoC alternatif (Intel Agilex, Xilinx Zynq, STM32MP2, ST STM32H7 si perception déportée).

### 8.5 Risque #5 — Concurrents subventionnés
Les acteurs souverains concurrents (Parrot en FR via DGA, Quantum Systems en DE via BMWi) bénéficient de subventions publiques. Sans soutien équivalent, HESIA a un CAC trop élevé pour le marché défense.

**Mitigation** : candidater à FEF, France 2030, EDF (European Defence Fund), Horizon Europe "EUCI".

### 8.6 Risque #6 — Responsabilité juridique
Un drone défense ou critique qui cause un incident et dont le firmware est compromis expose l'éditeur à une **responsabilité civile voire pénale**. Le CRA (2027) impose une obligation de diligence ("Security by design") qui serait mise à mal par les P0 actuels.

**Mitigation** : assurance cyber (RC Pro spécialisée, ~20-50 k€/an) + disclaimer contractuel + remédiation avant déploiement.

---

## 9. Scénarios de viabilité

### Scénario A — "Go défense maximum"
- Investir 1 à 2 M€ sur 18 mois pour remédiation + certification EAL4+ + CSPN.
- Lever 3-5 M€ seed.
- Cibler Thales / MBDA / Arquus en partenaires.
- **Pro** : valorisation haute si certification obtenue, marché en croissance.
- **Contra** : time-to-revenue 24-36 mois, risque d'échec certif ~30%.

### Scénario B — "Dual-use civil d'abord"
- Remédiation P0+P1 seule (6 mois, 200 k€).
- Positionnement inspection industrielle (EDF, Enedis, SNCF, TotalEnergies).
- Revenu 2027-2028, autofinance la certification défense.
- **Pro** : cash plus rapide, validation produit réelle.
- **Contra** : concurrence plus dense, marges inférieures, risque de "pivoter et ne jamais revenir".

### Scénario C — "Licensing technologique"
- Vendre la techno (pas le produit) à un intégrateur (Parrot, Safran, Thales).
- HESIA devient fournisseur d'IP + services.
- **Pro** : risque limité, valeur sortie rapide.
- **Contra** : valorisation plafonnée, perte d'indépendance, peu de upside.

### Scénario D — "Open source + support commercial"
- Publier le code (retire en partie l'avantage concurrentiel).
- Monétiser support + certification + intégration.
- **Pro** : communauté, crédibilité crypto (peer review), évite le coût de la défense seule.
- **Contra** : disrupte le positionnement "souverain", certains marchés (OIV) refusent le FOSS.

---

## 10. Recommandations économiques

1. **Priorité immédiate** : trancher le positionnement (défense pur vs dual-use). Le code actuel est compatible des deux, mais la stratégie commerciale et le budget diffèrent d'un facteur 3.

2. **Si défense pure** : candidater dès maintenant à un financement public (EDF call 2026, France 2030), prévoir une levée seed 2-3 M€, et lancer la remédiation + certification en parallèle.

3. **Si dual-use civil** : remédier P0+P1 (200 k€, 6 mois), approcher 3-5 clients pilotes (EDF, SNCF, TotalEnergies, Orange, RTE) avec des PoC à 30-50 k€ chacun → auto-financement.

4. **Investir dans le marketing / go-to-market** : le dossier `marketing/` vide est un signal fort. Sans un message clair, les commerciaux défense (cycle 3-5 ans) ne mordront pas.

5. **Construire une gouvernance de sécurité** : comité tech, policy de disclosure, programme bug bounty dès 2027. C'est une exigence implicite de tout acheteur sérieux.

6. **Plan de certification staged** :
   - T+6 mois : CSPN (objectif marché français).
   - T+18 mois : FIPS 140-3 Level 2 sur le module crypto.
   - T+30 mois : CC EAL4+ sur le TA + serveur.

7. **Capital humain** : recruter un **architecte crypto sénior** (profil ANSSI / militaire retraité) et un **directeur certification** (ex-LNE / ex-SERMA). Sans ces deux profils, la certification est un mur.

8. **Partenariats technologiques** :
   - Avec un fondeur de secure element (NXP, Infineon, STM) pour corriger Faille_01 long-terme.
   - Avec un acteur crypto certifié (Bull / Atos / Prim'X) pour crédibiliser.
   - Avec un laboratoire (LNE, SERMA, ANSSI-qualifié) pour la certification.

---

## 11. Conclusion économique

HESIA est **un projet technique avec un potentiel réel** mais dont le chemin vers la commercialisation demande **6 à 24 mois supplémentaires** selon l'ambition (dual-use vs défense pure) et **200 k€ à 2 M€ d'investissement**.

Le **risque majeur** identifié n'est pas la technique (le code est récupérable) mais :
- **L'absence de stratégie commerciale documentée** (marketing vide, pas de pricing, pas de clients pilotes identifiés).
- **La dette de sécurité actuelle** qui empêche tout déploiement sensible.
- **La taille d'équipe probablement insuffisante** pour porter le produit seul.

**Recommandation la plus prudente** : scénario B (dual-use civil d'abord) pour générer du CA tout en consolidant la base technique, puis bascule défense à T+18-24 mois avec une levée de fonds sur ce track record.

**Recommandation la plus ambitieuse** : scénario A (go défense) en partenariat avec un intégrateur établi (Parrot / Thales / Airbus Defence), ce qui offre crédibilité et canal de distribution, au prix d'une perte d'indépendance.

**À éviter** : tenter de faire les deux en parallèle avec une équipe réduite → dilution et échec probable sur les deux fronts.
