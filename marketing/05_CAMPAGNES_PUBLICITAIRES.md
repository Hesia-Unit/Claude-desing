# 05 — Campagnes Publicitaires & Hors-ligne

> Document opérationnel pour planifier les achats média (online + offline), salons, supports imprimés et relations partenaires. Le budget total cible est défini en `09_BUDGET_ROADMAP.md` ; ce document décrit le **comment** et le **où**, pas le **combien**.

## 1. Stratégie média globale

### 1.1 Principes directeurs

1. **Inbound > outbound froid**. Le contenu, le SEO et les RP attirent des prospects qualifiés. La pub paid sert à **accélérer** des audiences déjà chaudes — pas à compenser un manque de présence.
2. **Cibler le titre, pas le secteur seul**. CISO + énergie ≠ CISO + santé ≠ Directeur Innovation + transport. Une campagne unique générique gaspille 60% du budget.
3. **Pas de pub orientée volume**. HESIA a 30 comptes cibles top dans `00_ETUDE_DE_MARCHE.md`. On préfère 50 impressions auprès du DSI d'EDF que 50 000 clics tout-venant.
4. **Tester petit avant scale**. Tout media plan démarre en pilote 4-6 semaines, budget cap 5-10k€, KPI mesuré, avant scale.

### 1.2 Allocation budget pub (indicative, % du budget pub annuel)

| Levier | % budget | Cible |
|--------|----------|-------|
| LinkedIn Ads (sponsored content + InMail) | 40% | CISO, RSSI, Dir. Innovation, Acheteurs |
| Google Ads (Search) | 15% | Intent élevé, longue traîne PQC / NIS2 |
| Salons & événements | 25% | Présence physique, RDV qualifiés |
| Sponsoring presse spé / podcasts | 10% | Crédibilité, top of funnel |
| Display retargeting | 5% | Reciblage visiteurs site |
| Tests / nouveaux canaux | 5% | Bluesky Ads, Reddit, etc. |

## 2. LinkedIn Ads (canal principal)

### 2.1 Pourquoi LinkedIn Ads dominent

- L'audience B2B défense / OIV est sur LinkedIn et nulle part ailleurs.
- Le ciblage par titre + entreprise + secteur + ancienneté est imbattable.
- Format "Sponsored Content" + "Document Ads" + "InMail" couvre tout le funnel.
- Inconvénient : CPM élevé (40-90€), CPL élevé (150-400€). À assumer.

### 2.2 Audiences à construire (Matched Audiences)

**Audience 1 — Tier-1 CISO/RSSI grands comptes EU**
- Titre : CISO, Chief Information Security Officer, RSSI, Head of Cybersecurity, Director of Information Security
- Entreprise : top 30 comptes cibles (cf. `00_ETUDE_DE_MARCHE.md`) + opérateurs OIV listés ANSSI
- Géo : France, Allemagne, Pays-Bas, Belgique, Italie, Espagne, Suède, Pologne
- Taille audience estimée : 800-1500

**Audience 2 — Directeurs Innovation / Numérique**
- Titre : Director of Innovation, Chief Digital Officer, VP Engineering, Head of R&D
- Secteur : Energy & Utilities, Defense, Aviation & Aerospace, Logistics, Telecommunications
- Géo : EU
- Taille : 8000-15000

**Audience 3 — Acheteurs / Technical buyers défense**
- Titre : Procurement Director, Head of Procurement, Acquisition Manager
- Entreprise : DGA, OCCAR, ministères défense EU, primes défense (Thales, Airbus DS, Leonardo, Saab, KMW, MBDA)
- Géo : EU + UK + Norvège + Suisse
- Taille : 1500-3000

**Audience 4 — Communauté technique cybersécurité**
- Compétences : OP-TEE, TrustZone, Cryptography, Secure Boot, Embedded Systems, FIPS 140
- Titre : Security Engineer, Embedded Engineer, Cryptography Engineer
- Géo : EU
- Taille : 5000-10000
- Usage : recrutement + remarketing tech blog.

**Audience 5 — Custom audience visiteurs site**
- Pixel LinkedIn Insight Tag installé sur hesia.eu
- Visiteurs des pages /product/* et /solutions/*
- Window : 90 jours

**Audience 6 — Account-Based Marketing (ABM)**
- Liste manuelle 30 comptes cibles avec leurs employés
- LinkedIn Company List Targeting
- Combinée avec filtre titre

### 2.3 Formats et créatifs recommandés

**Format A — Document Ad (carrousel PDF)**
> Le format star pour HESIA. Convertit beaucoup mieux que single image.

- 6-10 slides PDF avec design brand HESIA (Steel Blue + Sovereign Gold).
- Sujets : "Migrer une flotte vers PQC en 4 étapes", "Décrypter NIS2 pour drones", "Attestation matérielle OP-TEE".
- CTA : "Télécharger le guide complet" → page /resources/[slug] avec form gated.
- Lead form natif LinkedIn pré-rempli (CPL plus bas qu'un form externe).

**Format B — Single Image Sponsored Content**
- Visuel sobre (gradient steel blue + texte court 5-8 mots).
- Texte au-dessus : 80-150 caractères, hook fort.
- CTA : Learn More → /product ou /demo.
- Tester 3 variations par audience.

**Format C — Video Ads (15-30s)**
- Animation motion design ou interview courte fondateur.
- Toujours avec sous-titres incrustés (85% des vues sans son).
- CTA fin : "Programmer une démo".

**Format D — Sponsored InMail**
- Réservé à Audience 1 et 3 (haute valeur).
- Personnalisation : prénom, entreprise, secteur dans le sujet.
- Long : 600-900 caractères max.
- CTA unique vers RDV Calendly ou page sectorielle.

### 2.4 Exemples créatifs (copy)

**Annonce 1 — Audience CISO**
```
🇪🇺 80% des drones d'inspection critique en EU n'ont pas de signature
de boot vérifiée. NIS2 vous oblige à le corriger.

HESIA Core sécurise les flottes UAS pour les opérateurs OIV avec :
✓ TLS 1.3 mTLS + post-quantique (ML-KEM-1024)
✓ Allowlist signée Ed25519
✓ Audit log immuable conforme NIS2 art. 21

Téléchargez notre playbook NIS2 + drones (gratuit, 24 pages, sans email
spam) → [CTA]
```

**Annonce 2 — Audience Innovation**
```
Vos drones d'inspection captent des données critiques sur votre réseau.
Comment vous prouvez à votre CISO que ces données ne fuitent pas ?

Dans notre nouveau livre blanc, on documente les 7 vulnérabilités les
plus fréquentes sur les flottes UAS industrielles — et leurs
remédiations.

Lecture : 18 min. Sans gating excessif.

→ [CTA]
```

**Annonce 3 — Audience Défense**
```
La cryptographie post-quantique n'est plus optionnelle pour les
systèmes embarqués. Le NIST a tranché en août 2024. Les agences
nationales aussi.

HESIA conçoit le firmware sécurisé qui équipe les drones européens
souverains : ML-KEM-1024, ML-DSA-87, OP-TEE, attestation matérielle.

Code source ouvert aux audits clients défense.

→ Programmer un échange technique [CTA]
```

**InMail 1 — CISO grand compte (300 mots)**
```
Bonjour [Prénom],

Je suis [Fondateur], cofondateur de HESIA. Nous travaillons sur la
sécurité des drones et systèmes autonomes embarqués.

J'écris parce que [Entreprise] opère ce qui est probablement l'une des
flottes UAS d'inspection les plus stratégiques du secteur [secteur] en
Europe — et parce que les obligations NIS2 / CRA qui s'appliquent
aux composants embarqués critiques arrivent vite.

Je ne pitche rien aujourd'hui. Je voulais simplement partager deux
ressources qui pourraient être utiles à vos équipes :

1. Notre matrice de conformité NIS2 appliquée aux UAS (PDF, 12 pages).
2. Notre benchmark technique ML-KEM / ML-DSA sur Jetson Orin (post de
   blog public).

Si vos équipes regardent activement la migration post-quantique de
votre flotte, je serais ravi d'échanger 30 minutes — sans agenda
commercial, juste pour comparer notes et architectures.

[Lien Calendly]

Bien cordialement,
[Fondateur]
HESIA · hesia.eu
```

### 2.5 Mesure et optimisation

- **KPI primaires** : Cost Per Lead (CPL), Cost Per Demo (CPD), Cost Per Closed Deal (CPCD).
- **Cibles indicatives 2026** : CPL < 250€, CPD < 800€, CPCD < 8000€ pour cycle 6-12 mois.
- **Cycle de revue** : weekly pendant les 8 premières semaines, puis bi-weekly.
- **Optimisation** : pause toute annonce sous CTR < 0.4% après 2000 impressions ; iterate copy, pas seulement le visuel.

## 3. Google Ads (Search)

### 3.1 Stratégie

Couverture **search intent élevé** sur 4 groupes thématiques. Pas de display generic. Pas de YouTube preroll (ROI faible pour B2B niche).

### 3.2 Groupes de mots-clés

**Groupe 1 — Post-Quantum Cryptography (FR + EN)**
- "cryptographie post-quantique" / "post-quantum cryptography"
- "ml-kem implémentation" / "ml-kem implementation"
- "migration cryptographique pqc"
- "nist fips 203"
- "harvest now decrypt later"
- "cnsa 2.0"

**Groupe 2 — Drone Security (FR + EN)**
- "sécurité drone" + "industrielle" / "professional"
- "drone cybersecurity"
- "uas hardening"
- "secure boot drone"

**Groupe 3 — NIS2 / CRA Compliance**
- "nis2 directive systèmes embarqués"
- "cyber resilience act drone"
- "anssi drone homologation"

**Groupe 4 — Embedded TEE**
- "op-tee trusted application"
- "trustzone implementation"
- "secure boot jetson"
- "rpmb authentication"

### 3.3 Budget et structure de campagne

- 4 campagnes (1 par groupe), budget mensuel initial 800€/campagne = 3200€/mois.
- Enchères manuelles plafonnées au CPC max (2-4€).
- Landing pages dédiées par groupe avec form qualification courte.
- Suivi conversions configuré : form submit, demo request, whitepaper download.

### 3.4 Pages d'atterrissage par campagne

| Campagne | URL landing |
|----------|-------------|
| PQC | /resources/pqc-migration-guide |
| Drone Security | /solutions/critical-infrastructure |
| NIS2 / CRA | /resources/nis2-uas-compliance |
| OP-TEE | /resources/optee-secure-boot-jetson |

## 4. Salons & événements physiques

### 4.1 Sélection (priorité 12 prochains mois)

| Salon | Lieu | Date | Catégorie | Présence recommandée |
|-------|------|------|-----------|---------------------|
| **FIC (Forum International de la Cybersécurité)** | Lille | Avril | Cyber généraliste | Présence stand + speaking |
| **UAV Show** | Bordeaux | Octobre | Drones civils + dual-use | Stand + démo |
| **Milipol Paris** | Paris | Novembre (années paires) | Sécurité intérieure | Stand qualifié défense |
| **Eurosatory** | Paris | Juin (années paires) | Défense terrestre | Présence visiteur (pas stand) |
| **AI for Defense Summit** | Paris | T1 | Défense IA | Speaking si invité |
| **NIST Post-Quantum Conference** | Washington / online | T1 | Tech crypto | Speaking + booth virtuel |
| **DEF CON / Black Hat EU** | Vegas / Londres | Août / Décembre | Tech sécurité | Speaking research talk |
| **Conférence ANSSI / Symposium SSTIC** | Rennes | Juin | Cyber FR | Speaking research talk |
| **Hannover Messe** | Hanovre | Avril | Industrie 4.0 | Visiteur (lead scouting) |
| **Hexagone Cyber** | Paris | Variable | Cyber FR souverain | Stand |

### 4.2 Stratégie présence

**Approche P0 (FIC, UAV Show, Milipol)** : stand + speaking + RDV qualifiés en amont.
- Budget par événement (stand + voyage + matériel) : 15-30k€.
- Goal : 80-150 contacts qualifiés par événement, 15-25 RDV qualifiés post-event.

**Approche P1 (Eurosatory, AI Defense Summit)** : visiteur seulement, RDV pré-bookés.
- Budget : 2-5k€ (passes + voyage).
- Goal : 10-20 RDV de prospection pré-bookés.

**Approche P2 (DEF CON, SSTIC)** : speaking uniquement (pas de stand).
- Budget : 5-15k€.
- Goal : crédibilité technique + recrutement + presse.

### 4.3 Stand HESIA — design type

- Surface : 9 à 12m² minimum.
- Mur de fond : grand visuel brand (gradient steel blue + tagline) + logo backlit.
- Démo statique : drone Jetson Orin avec écran live montrant attestation OP-TEE + handshake PQC.
- Démo interactive : terminal CLI permettant de signer une policy et observer le rejet d'un firmware non signé.
- Comptoir d'accueil + 2 tables de conversation.
- Roll-ups : 3 (1 produit, 1 use case secteur, 1 roadmap).
- Goodies : aucun gadget plastique. **Plaquette A5 + sticker brand de qualité + carte NFC vCard fondateur**. C'est tout.
- Catering offert : café de spécialité (signal de soin), 1 jour sur 2.

### 4.4 Pré-événement (J-30 → J-7)

- Liste des inscrits reçue → matching avec base CRM HESIA.
- Outreach LinkedIn personnalisé aux comptes cibles présents (50 max).
- Annonce sur LinkedIn page (3 posts répartis).
- Email partenaires + newsletter avec teaser présence.
- 2-3 RDV pré-bookés par jour minimum.

### 4.5 Post-événement (J+1 → J+14)

- Tous les contacts saisis dans CRM dans les 48h.
- Email de remerciement personnalisé J+2 (pas un mass-mail générique).
- LinkedIn connect avec note pour les RDV qualifiés.
- Suivi téléphone J+7 pour les leads chauds.
- Bilan interne J+14 : ROI, ajustements pour prochain événement.

## 5. Presse & Relations Médias

### 5.1 Cibles presse spécialisée

**Tier 1 — Tech / Cyber FR**
- Le Monde Informatique
- L'Usine Digitale / L'Usine Nouvelle
- 01net Pro
- ZDNet France
- Silicon.fr
- LeMagIT
- L'Usine Nouvelle (industrie)

**Tier 1 — Cyber EN/EU**
- The Register
- Dark Reading
- SecurityWeek
- Bleeping Computer
- Help Net Security
- Risky Business (podcast)

**Tier 2 — Défense / Aéro**
- Air & Cosmos
- Defense News
- Janes
- Forces Operations Blog
- Breaking Defense

**Tier 2 — Quantum / Crypto**
- IEEE Spectrum
- Quantum Computing Report
- Inside Quantum Technology News

**Tier 3 — Géopolitique / Souveraineté**
- Le Grand Continent
- Politico EU
- Contexte (FR)

### 5.2 Plan RP 12 mois

| Mois | Action |
|------|--------|
| M+1 | Communiqué lancement HESIA — diffusion Tier 1 cyber |
| M+3 | Contribution tribune signée fondateur sur PQC EU (Le Monde / Politico EU) |
| M+5 | Annonce premier client pilote (sous accord) |
| M+7 | Publication livre blanc PQC + relais presse spé |
| M+9 | Tribune sur souveraineté cyber EU (timing actu géopolitique) |
| M+12 | Bilan année + roadmap (interview fondateur dans média Tier 1) |

Détails opérationnels (kits presse, communiqués type, contacts) : voir `10_RELATIONS_PRESSE.md`.

### 5.3 Pas de "stunt RP"

- Pas de "live hack démo" sur scène (cliché et juridiquement risqué).
- Pas de communiqué "we are excited to announce" hebdomadaire.
- Pas d'attaque publique d'un concurrent.
- Pas d'ouverture média sur clients sans accord écrit explicite.

## 6. Sponsoring podcast

### 6.1 Cibles podcast (FR + EN)

**FR**
- *NoLimitSecu* — communauté cyber FR, audience CISO/RSSI.
- *Dans les Coulisses du Cyber* — RPGD / OIV.
- *Le Comptoir Sécu* — entrepreneurs cyber.

**EN**
- *Risky Business* — référence mondiale cyber (audience exécutif).
- *CyberWire Daily*.
- *Smashing Security* (touch grand public technique).
- *The Tech Lead Journal* (tech leadership).

### 6.2 Modèle de sponsoring

- Pre-roll 60-90s lu par l'animateur (pas voix off générique).
- Lien tracké : hesia.eu/[podcast-name]
- Code promo whitepaper exclusif (ex : "Use code RISKYBIZ for early access").
- Budget indicatif : 1500-5000€ par épisode selon audience.
- Test sur 4 épisodes avant scale.

## 7. Brochures et supports imprimés

### 7.1 Brochure produit principale (12 pages, A4)

**Sommaire** :
1. Couverture — visuel brand + tagline.
2. Le problème en chiffres (NIS2, CRA, PQC, supply chain).
3. La plateforme HESIA en 1 schéma.
4. HESIA Core — fonctionnalités détaillées.
5. HESIA Command — supervision multi-drones.
6. HESIA Observe — IA embarquée auditable.
7. HESIA Attest — attestation matérielle OP-TEE.
8. Conformité réglementaire (matrice NIS2 / CRA / ANSSI).
9. Stack technique transparente (composants opensource cités).
10. Roadmap 24 mois.
11. Témoignages clients (quand disponibles, sinon partenaires techniques).
12. Contact + QR code site + signature crypto.

**Tirage** : 500 exemplaires premium (papier 200g, finition mate, dorure or sur logo).
**Coût** : 1500-2500€ pour 500 ex.
**Distribution** : salons, RDV, presse.

### 7.2 One-pagers sectoriels (A4 recto-verso)

Un par persona / verticale :
- Critical Infrastructure (énergie, transport, télécoms)
- Defense (forces armées, primes)
- Integrators (intégrateurs systèmes)

### 7.3 Carte NFC fondateur

Carte vCard NFC haut de gamme (PVC métal). Tap → ouvre profil LinkedIn fondateur + carte de visite vCard. Coût : 30-50€ par carte. Offre signal de soin et remplace 90% des cartes de visite papier perdues.

### 7.4 Goodies

- ❌ Stylos / stress balls / mug logo.
- ✅ Sticker brand qualité (300 ex).
- ✅ Carnet Moleskine avec gravure logo discrète (50 ex pour leads chauds).
- ✅ T-shirt brand simple (200 ex équipe + grands events).

## 8. Display & Retargeting

### 8.1 Pixel et tracking

- LinkedIn Insight Tag.
- Google Ads tag.
- Plausible (analytics légère, RGPD-friendly, pas de cookie).
- **Pas de** : Facebook Pixel, TikTok Pixel, Meta. Pas pertinent et pose des questions souveraineté/RGPD pour notre cible.

### 8.2 Audiences retargeting

- Visiteurs site 30 jours non convertis → LinkedIn Sponsored Content.
- Visiteurs pages /product/* → Document Ad "Comparer Core / Command".
- Téléchargeurs whitepaper → Sponsored InMail "Programmer démo".

## 9. Programme partenaires

### 9.1 Partenaires intégrateurs

3 catégories :
- **Cabinets cyber** (Wavestone, Sopra Steria CYBR, Quarkslab, Synacktiv) → revente + audit.
- **Intégrateurs aéro/défense** (Capgemini, Atos, Sopra) → intégration projets clients.
- **Distributeurs HW drone** (constructeurs / revendeurs Jetson) → bundle HW+SW.

### 9.2 Modèle commercial partenaire

- Marge 20-30% sur licence SW.
- Formation technique 2 jours offerte (premier collaborateur certifié).
- Co-marketing : un webinar par trimestre.
- Co-publication : 1 case study commun par an.

### 9.3 Partenaires technologiques

Pas de revente, mais visibilité croisée :
- liboqs / Open Quantum Safe.
- OP-TEE Project (Linaro).
- Yocto Project.
- Renesas / NXP / NVIDIA Jetson (programme partenaires HW).

Bénéfices : badges officiels, page partenaires, visibilité conférences.

## 10. Programme advocacy / ambassadeurs

### 10.1 Identifier les "champions" externes

3-5 personnes externes à HESIA qui adoreraient le produit et le citeraient publiquement :
- Chercheurs en cryptographie post-quantique.
- Anciens ANSSI / DGA reconvertis.
- CISOs qui parlent publiquement (LinkedIn, Twitter).
- Bloggers / podcasters cyber.

### 10.2 Programme

- Accès anticipé au produit + roadmap privée.
- Invitations événements (dîners techniques fermés).
- Co-publication possible (sans rémunération directe — préserve la crédibilité).
- Mentions publiques bidirectionnelles.

**À ne PAS faire** : payer un influenceur LinkedIn pour un post sponsorisé. Détecté immédiatement par la cible et nuit à la crédibilité.

## 11. Calendrier média année 1

| Mois | LinkedIn Ads | Google Ads | Salons | Presse | Podcast |
|------|-------------|------------|--------|--------|---------|
| M+1 | Lancement audience 1 (test 5k€) | Setup + groupe PQC | — | Communiqué lancement | — |
| M+2 | Scale audience 1, lancement audience 2 | Groupe Drone Security | — | — | Test 1 podcast |
| M+3 | Test InMail audience 3 | Optimisations | FIC (présence) | Tribune fondateur | — |
| M+4 | Scale audience 2 | Groupe NIS2 / CRA | — | — | 2 podcasts FR |
| M+5 | Test retargeting | Optimisations | — | Annonce client pilote | — |
| M+6 | Document Ads | Groupe OP-TEE | Test conférence tech | — | — |
| M+7 | Scale full audiences | Scale | — | Livre blanc relais | 1 podcast EN |
| M+8 | Optimisations | — | — | — | — |
| M+9 | — | — | — | Tribune souveraineté | — |
| M+10 | Test audience 4 (recrutement) | — | UAV Show (stand) | — | — |
| M+11 | — | — | Milipol Paris (stand) | Press kit Milipol | — |
| M+12 | Bilan + repositionnement | Bilan | — | Bilan année | — |

## 12. KPI campagnes

| KPI | T+6 mois | T+12 mois |
|-----|----------|-----------|
| Leads totaux générés (paid) | 80 | 250 |
| MQL (Marketing Qualified Lead) | 30 | 100 |
| SQL (Sales Qualified Lead) | 12 | 40 |
| Demos planifiées | 8 | 25 |
| Pilotes signés | 1 | 3 |
| Coût par lead moyen | < 250€ | < 200€ |
| ROAS sur 18 mois | — | > 4x |

Reporting détaillé : voir `08_KPI_DASHBOARD.md`.

---

**Dernière mise à jour** : 2026-04-25
**Maintenu par** : Marketing & Communication HESIA
