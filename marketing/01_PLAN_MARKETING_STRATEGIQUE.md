# HESIA — Plan Marketing Stratégique 2026-2028

**Horizon** : 24 mois glissants
**Budget indicatif** : voir `09_BUDGET_ROADMAP.md`
**Équipe cible** : 1 CMO + 1 growth / content + 1 BDR (business development rep) la première année.

---

## 1. Vision marketing

> **"HESIA rend les drones européens sécurisables par conception, à l'ère post-quantique."**

En 2028, HESIA doit être **le premier nom cité** quand un CISO ou un directeur de programme drone européen cherche à sécuriser une flotte autonome sensible. Pas le plus connu du grand public : le plus respecté des 5 000 décideurs qui comptent.

---

## 2. Positionnement

### 2.1 Énoncé de positionnement (positioning statement)

> **Pour** les opérateurs d'infrastructures critiques et les intégrateurs drones européens
> **Qui doivent** sécuriser leurs flottes autonomes face aux exigences réglementaires (NIS2, CRA) et aux menaces avancées (quantum-ready attackers)
> **HESIA est** une plateforme logicielle souveraine de sécurisation firmware + serveur
> **Qui** apporte la cryptographie post-quantique validée NIST et un ancrage matériel OP-TEE
> **Contrairement à** Skydio Defense (ITAR, fermé) ou aux solutions crypto classiques (RSA/ECC obsolètes à horizon 2030)
> **Notre produit est** open-architecture, audité, et conçu en Europe pour les cas d'usage critiques.

### 2.2 Trois promesses de marque

1. **Souverain** : 100 % conçu en Europe, pas d'ITAR, pas d'EAR, pas de kill switch.
2. **Post-quantique natif** : ML-KEM-1024 + ML-DSA-87 implémentés via OpenSSL/liboqs, conformes FIPS 203/204.
3. **Auditable** : code source fourni aux clients qualifiés, audits tiers publiés.

### 2.3 Ce qu'HESIA n'est **pas** (anti-positioning)
- Pas un fabricant de drones (on vend la couche sécurité).
- Pas un OS temps réel (on s'intègre à Linux / PX4 / ArduPilot).
- Pas une solution consumer-grade (pas de DJI-like).
- Pas un framework recherche académique (production-grade).

---

## 3. Marché cible & personae (rappel — détails dans `00_ETUDE_DE_MARCHE.md`)

| Persona | Phase d'approche | Priorité |
|---------|------------------|----------|
| Claire — CISO OIV | Phase 1 | P1 |
| Marc — Directeur Innovation | Phase 1 | P1 |
| Stéphane — CTO scale-up drone | Phase 1 | P2 |
| Col. Vasseur — DGA | Phase 2 (T+18+) | P3 |

---

## 4. Mix Marketing 4P

### 4.1 Product

#### Offre principale
**HESIA Secure Drone Firmware Suite** — licence logicielle embarquée.

Modules :
- **HESIA Core** — firmware drone avec PQC + TEE + TLS mTLS.
- **HESIA Command** — serveur de contrôle avec policy signée.
- **HESIA Observe** — pipeline vidéo chiffré (VideoChannel + CleanPipeline).
- **HESIA Attest** — attestation à distance HW-bound (roadmap Q2 2027 après correction Faille_12).

#### Offre complémentaire
- **HESIA Assessment** — audit 5 jours sur le firmware client existant.
- **HESIA Integration** — prestation intégration PX4 / ArduPilot / ROS2.
- **HESIA Support Premium** — SLA 4h business, 24h hors heures.

### 4.2 Price

#### Modèle tarifaire (phase 1)
| Offre | Prix catalogue | Conditions |
|-------|----------------|------------|
| HESIA Core (licence par drone) | 2 500 €/drone/an | Volume 10+ |
| HESIA Core (licence perpétuelle) | 8 000 € + 1 200 €/an maintenance | Volume 50+ |
| HESIA Command (licence serveur) | 15 000 €/an | Jusqu'à 50 drones |
| HESIA Command Enterprise | 40 000 €/an | Illimité |
| HESIA Assessment | 20 000 € forfait | 5 jours ouvrés |
| HESIA Integration | 800 €/jour | Min 20 jours |
| HESIA Support Premium | 12 % licences | Par an |

**Philosophie pricing** :
- Phase 1 (2026) : aligner sur concurrents civils (Delair, Parrot). Faire du volume pour construire les références.
- Phase 2 (2027+) : premium defense +30 à 50 % vs. phase 1. L'obtention du CSPN + CC EAL4+ justifie l'écart.

#### Pricing ancré par la valeur
- **Un drone d'inspection compromis** = incident médiatique + coût investigation ≈ **500 k€ à 2 M€**.
- Une licence HESIA à 2 500 €/an = **0,5 % du coût d'incident**. Argument ROI solide.

### 4.3 Place (distribution)

#### Canaux directs
- **Sales direct** (CMO + BDR + futur Head of Sales).
- **Site web hesia.eu** avec formulaire démo.
- **GitHub public** (lib PoC open source en showcase).

#### Canaux indirects
- **Intégrateurs drones** (Parrot, Delair, Flying Whales, Quantum Systems DE) : accord de revente 20-30 % marge.
- **Cabinets conseil cyber** (Wavestone, Accenture Security, Sopra Steria, Hexatrust membres) : co-prescription.
- **Distributeurs spécialisés défense** (OCCAR, DGA "catalogues") : phase 2.

#### Partenariats technologiques
- **PQShield** : IP cores PQC silicium.
- **Thales LynX / LynK OS** : co-intégration future OS durci.
- **Quarkslab / Synacktiv** : audit tiers + pentest client.

### 4.4 Promotion

Détails dans `05_CAMPAGNES_PUBLICITAIRES.md`, `06_CONTENT_MARKETING.md`, `10_RELATIONS_PRESSE.md`.

Mix canaux :
- **Content marketing** (blog, livres blancs) : 40 % effort.
- **LinkedIn organic + ads** : 25 %.
- **Événements / salons** : 15 %.
- **PR / relations presse** : 10 %.
- **SEO** : 5 %.
- **Google Ads ciblés** : 5 %.

---

## 5. Roadmap marketing 24 mois

### Q2 2026 (T+0 à T+3 mois) — **Fondation**
- [ ] Identité de marque finalisée (logo, charte).
- [ ] Site web hesia.eu en ligne.
- [ ] LinkedIn page + CEO/CTO profils optimisés.
- [ ] 2 articles de blog techniques (PQC drones, TEE attestation).
- [ ] 1 livre blanc "Post-Quantum Readiness pour drones industriels".
- [ ] 5 prospects P1 contactés manuellement.
- [ ] Inscription Hexatrust + Pôle SCS + Aerospace Valley.

### Q3 2026 (T+3 à T+6 mois) — **Crédibilisation**
- [ ] 1 webinaire "NIS2 & drones : ce que les OIV doivent savoir" (co-hosté avec Wavestone ou cabinet similaire).
- [ ] Participation observateur FIC Lille.
- [ ] 3 articles de blog + 1 livre blanc "Architecture de sécurité drone de bout en bout".
- [ ] Podcast interview (NoLimitSecu, Geekosecure, HackInScience).
- [ ] 15 prospects P1 en conversation active.
- [ ] 1 POC signé.

### Q4 2026 (T+6 à T+9 mois) — **Accélération**
- [ ] Premier salon exposant : UAV Show Bordeaux (niveau "Start-up pack").
- [ ] Campagne LinkedIn Ads ciblée CISO OIV EU (budget 30 k€ sur 3 mois).
- [ ] Publication d'un audit tiers du code par Quarkslab ou Synacktiv.
- [ ] 2 POC signés.
- [ ] 3 livres blancs publiés au total, 10 articles blog.

### Q1 2027 (T+9 à T+12 mois) — **Validation**
- [ ] Premier client en production (launch case study).
- [ ] Dépôt CSPN ANSSI.
- [ ] Conférence invité (SSTIC, Pass The Salt, Black Alps).
- [ ] Webinaire "Post-Quantum Roadmap pour les Operators" (200 inscrits cible).
- [ ] 50+ leads qualifiés dans CRM.

### Q2-Q3 2027 (T+12 à T+18 mois) — **Expansion civile**
- [ ] 5 clients en production.
- [ ] Présence à FIC Lille (stand).
- [ ] Publication "HESIA Security Report 2027" (rapport annuel).
- [ ] Expansion DACH (Allemagne + Suisse).
- [ ] CSPN obtenu → communication dédiée.
- [ ] Début travaux CC EAL4+.

### Q4 2027 - Q1 2028 (T+18 à T+24 mois) — **Pivot défense**
- [ ] Premier contrat DGA (via AID ou EDF call).
- [ ] Salon Milipol (exposant).
- [ ] Extension vers OTAN Innovation Fund.
- [ ] 10+ clients en production.
- [ ] ARR ~3 M€ (hypothèse médiane).

---

## 6. Objectifs chiffrés (OKR)

### Objectif annuel 2026

**Objective O1** : Construire la crédibilité marque dans l'écosystème cybersécurité EU.
- KR1 : 10 000 visiteurs uniques sur hesia.eu sur 2026.
- KR2 : 1 500 abonnés LinkedIn page.
- KR3 : 3 articles de presse tier 1 (Les Échos, La Tribune, JDN, Silicon).
- KR4 : 5 invitations conférences (speaker).

**Objective O2** : Générer un pipeline commercial initial.
- KR1 : 100 leads qualifiés (MQL) dans le CRM.
- KR2 : 30 conversations discovery avec personae P1/P2.
- KR3 : 3 POC signés.
- KR4 : 1 contrat production signé (~50-200 k€).

**Objective O3** : Construire l'asset content.
- KR1 : 3 livres blancs majeurs publiés.
- KR2 : 24 articles de blog publiés (cadence 2/mois).
- KR3 : 6 webinaires animés.
- KR4 : 1 podcast mensuel lancé (épisode pilote Q3).

### Objectif annuel 2027

**Objective O1** : Conversion pipeline → revenu.
- KR1 : ARR 1 M€ → 3 M€.
- KR2 : 10 clients production actifs.
- KR3 : CAC / LTV < 1/3.

**Objective O2** : Expansion EU.
- KR1 : 2 pays couverts hors France (DACH + BeNeLux).
- KR2 : 1 partenariat intégrateur majeur (Quantum Systems, Delair ou équivalent).
- KR3 : CSPN obtenu.

**Objective O3** : Préparation phase défense.
- KR1 : 2 premiers contacts DGA / AID.
- KR2 : 1 consortium EDF call déposé.
- KR3 : Projet CC EAL4+ en cours.

---

## 7. Messaging framework

### 7.1 Pitch en 10 secondes (elevator)

> HESIA sécurise les drones industriels et défense européens avec la cryptographie post-quantique, pour répondre à NIS2, CRA et à la menace quantique de 2030.

### 7.2 Pitch en 30 secondes (discovery)

> HESIA est une suite logicielle embarquée qui durcit les drones et leurs serveurs de contrôle. On apporte quatre choses : la cryptographie post-quantique standardisée NIST (ML-KEM, ML-DSA), un ancrage matériel OP-TEE pour l'attestation, un transport mTLS avec policy signée, et un pipeline vidéo chiffré. On s'intègre à PX4, ArduPilot, ROS2. Nos clients sont des opérateurs d'infrastructures critiques et des intégrateurs drones en France et en Europe. On est alignés avec NIS2, préparé pour le CRA 2027 et la CNSA 2.0 OTAN.

### 7.3 Pitch en 2 minutes (investor / executive)

> Les drones deviennent critiques pour les infrastructures européennes — énergie, transport, défense. Mais 80 % des firmwares drones utilisent encore RSA ou ECC, qui seront cassables par les ordinateurs quantiques à horizon 2030. Parallèlement, NIS2 force les OIV à sécuriser tous leurs systèmes OT/IoT, et le Cyber Resilience Act imposera en 2027 une obligation légale de "security by design".
>
> HESIA répond à cela avec une suite logicielle complète : firmware drone + serveur de contrôle + ancrage TEE + cryptographie post-quantique validée NIST. On s'intègre aux plateformes existantes sans remplacer le hardware. C'est souverain, européen, open-architecture, et auditable.
>
> Notre stratégie : conquérir d'abord le marché inspection industrielle civile pour bâtir nos références (EDF, SNCF, Orange...), puis basculer vers la défense une fois les certifications CSPN et CC EAL4+ obtenues. Le marché adressable EU est à 4-8 Md€ sur nos segments cibles 2027-2030. Nous visons 15-25 M€ ARR sur 5 ans.

### 7.4 Tag lines (variantes)

Principale :
> **"Sovereign security, post-quantum ready."**

Secondaires selon audience :
- Technique : *"Post-quantum cryptography. Hardware-anchored. Audited."*
- Compliance : *"NIS2-aligned. CRA-ready. ANSSI-compliant."*
- Défense : *"Trusted firmware for European autonomy."*
- FR : *"La sécurité souveraine des drones critiques."*

---

## 8. Facteurs clés de succès

| FCS | Comment on le mesure | Risque si raté |
|-----|----------------------|------------------|
| Obtenir 3 case studies publiques d'ici fin 2026 | Nombre de case studies signés | Sans preuve, pipeline défense impossible |
| Être vu à SSTIC / FIC / UAV Show | Nombre d'événements participés | Marque invisible dans l'écosystème |
| Passer CSPN en 2027 | Date obtention certificat | Phase défense bloquée |
| Recruter 1 "tête d'affiche" crypto (ex ANSSI ou ex académique) | Embauche effective | Crédibilité technique plafonnée |
| Maintenir discipline content marketing 2 posts/mois | Cadence tenue 80 %+ | SEO + thought leadership s'effondrent |
| Séparer discours "dual-use civil" vs "défense" | Analytics segmenté LP civil / défense | Confusion des messages → perte des deux cibles |

---

## 9. Ce qu'on ne fait PAS (trade-offs assumés)

- **Pas de market US avant Phase 3** (T+36+). Certifications différentes (FIPS, NIAP, JITC), coût prohibitif avant traction EU.
- **Pas de B2C**, pas de drones grand public.
- **Pas de salons non stratégiques** (CES, Mobile World Congress). Focus 3 salons/an max.
- **Pas de social media généraliste** (TikTok, Instagram). LinkedIn + X + YouTube uniquement.
- **Pas d'influenceurs marketing**. Uniquement KOL techniques reconnus.
- **Pas de logo awards / programme "best in class"**. Reconnaissance via clients et publications.

---

## 10. Synthèse visuelle du plan

```
  2026                   2027                   2028
  ──────────────────────┬──────────────────────┬────────────
  FONDATION             │ VALIDATION            │ SCALE
                        │                       │
  Brand + Site          │ CSPN filed → obtained │ CC EAL4+ in progress
  LinkedIn + 3 WP       │ 10 clients prod       │ First DGA contract
  1er POC → 1er contrat │ 3 pays EU             │ 25 clients prod
  10k visiteurs/an      │ ARR 1-3 M€            │ ARR 5-10 M€
                        │                       │
  BUDGET MKT            │ BUDGET MKT            │ BUDGET MKT
  ~350 k€               │ ~600 k€               │ ~900 k€
```

Détails du budget : `09_BUDGET_ROADMAP.md`.
Détails KPI : `08_KPI_DASHBOARD.md`.
Détails sales funnel : `07_SALES_FUNNEL.md`.
