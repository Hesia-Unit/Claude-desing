# 04 — Stratégie Réseaux Sociaux

> **Objectif** : construire une présence en ligne crédible auprès des CISO, RSSI, directeurs innovation et acheteurs défense — sans tomber dans le marketing de surface. Le canal principal est **LinkedIn** (B2B défense / OIV). Les autres canaux sont **secondaires** et n'ouvrent que si on a la bande passante éditoriale pour les soutenir.

## 1. Cartographie des canaux

| Canal | Priorité | Audience cible | Fréquence | Effort |
|-------|----------|----------------|-----------|--------|
| LinkedIn (Page + perso fondateurs) | P0 | CISO / RSSI / DGSI / DGA / dir. innovation | 3-5 posts/semaine | Élevé |
| GitHub | P0 | Devs / ingénieurs cyber / auditeurs | Releases + docs | Moyen |
| X (ex-Twitter) | P1 | Communauté cyber FR/EU, journalistes tech | 5-10 posts/semaine | Moyen |
| YouTube | P1 | Démos techniques, replays webinaires | 1-2 vidéos/mois | Élevé |
| Bluesky | P2 | Communauté cyber post-Twitter, chercheurs | 3-5 posts/semaine | Faible |
| Mastodon (infosec.exchange) | P2 | Chercheurs sécurité, communauté FOSS | 2-3 posts/semaine | Faible |
| Instagram / TikTok | ❌ | — | — | — |

**Règle générale** : on ouvre un canal seulement quand on peut tenir 3 mois consécutifs de publications. Un canal abandonné nuit à la crédibilité.

## 2. LinkedIn (canal principal)

### 2.1 Page entreprise

**Nom** : HESIA
**Tagline** : Sovereign security, post-quantum ready.
**Slogan court** (sous logo) : Securing the autonomous edge.
**Industrie** : Computer & Network Security
**Taille** : 2-10 employés
**Spécialités (10 max)** :
- Post-Quantum Cryptography
- Drone Security
- Embedded Systems
- Trusted Execution Environment
- ML-KEM / ML-DSA
- NIS2 Compliance
- Cyber Resilience Act
- Sovereign Tech
- Critical Infrastructure
- Defense

**Description courte (À propos)** :
```
HESIA conçoit le firmware sécurisé qui protège les drones et systèmes
autonomes critiques contre les menaces actuelles et l'arrivée des
ordinateurs quantiques.

Notre plateforme combine cryptographie post-quantique standardisée
(ML-KEM-1024, ML-DSA-87 — NIST FIPS 203/204), exécution en environnement
de confiance (OP-TEE), et signature de politique opérateur — pour donner
aux opérateurs européens d'infrastructures critiques et de défense un
contrôle souverain sur leurs flottes.

Basée en France. Conçue pour la conformité NIS2, le Cyber Resilience Act
et l'autonomie stratégique européenne.

🔒 hesia.eu
```

**Bannière** : visuel sobre — gradient steel blue → graphite avec mention `Sovereign security, post-quantum ready.` et logo HESIA en bas-droite. Pas de drone fictif, pas de stock photo. Image vectorielle propre.

### 2.2 Profils fondateurs

Les profils perso convertissent **3-5x mieux** que la page entreprise sur LinkedIn B2B. Investir lourdement dessus.

**Template profil fondateur (CTO / sécurité)** :
- **Headline** : Founder & Security Lead @ HESIA · Post-quantum cryptography for autonomous systems · ex-[ANSSI / Thales / Airbus / etc.]
- **À propos** :
  ```
  Je travaille sur la sécurité des drones européens à l'ère post-quantique.

  Chez HESIA, nous concevons le firmware durci, l'attestation matérielle
  et les protocoles de communication qui empêchent un attaquant
  (étatique ou non) de prendre le contrôle d'un drone d'inspection
  industrielle, de surveillance ou de défense.

  Avant HESIA : [parcours technique précis — pas de buzzwords].

  Sujets que je creuse publiquement :
  → Migration vers ML-KEM / ML-DSA (NIST FIPS 203/204)
  → OP-TEE et secure boot sur Jetson / iMX / Rockchip
  → NIS2 / CRA appliqués aux UAS
  → Souveraineté européenne et chaîne d'approvisionnement

  Si vous opérez une flotte de drones critique, parlons-en : [email].
  ```
- **Expérience** : poste actuel + 2-3 postes précédents avec des résultats mesurables, pas du fluff.
- **Formation** : INSA / ENSTA / Polytechnique / etc. — diplômes vérifiables uniquement.
- **Compétences (top 5 épinglées)** : Cryptographie / Sécurité embarquée / OP-TEE / Architecture sécurité / Conformité.
- **Recommandations** : viser 5-10 recommandations LinkedIn de pairs techniques (anciens collègues, chercheurs).

### 2.3 Calendrier éditorial type (semaine)

| Jour | Type | Auteur | Format |
|------|------|--------|--------|
| Lundi | Veille / news cyber commentée | Page HESIA | Texte 150-300 mots + lien |
| Mardi | Post technique deep-dive | Profil fondateur | Texte 500-800 mots + schéma |
| Mercredi | — | — | (off ou repost) |
| Jeudi | Étude de cas / use case sectoriel | Page HESIA | Texte + visuel |
| Vendredi | Réflexion stratégique / opinion | Profil fondateur | Texte 400-600 mots |

**Règle** : 70% contenu utile (pédagogique, technique, décryptage), 20% contenu projet (sortie produit, milestone, équipe), 10% contenu personnel/humain (coulisses, valeurs).

### 2.4 Première série de 12 posts (2 mois de runway)

Les posts ci-dessous sont prêts à être adaptés et publiés. Ton : technique, sobre, factuel.

---

**Post #1 — Lancement page (jour 0)**

```
HESIA est en ligne.

Pourquoi maintenant : trois forces convergent.

1️⃣ Le NIST a publié les standards de cryptographie post-quantique
en août 2024 (FIPS 203, 204, 205). Les agences nationales (ANSSI, BSI,
NSA) imposent désormais des roadmaps de migration. Les drones et
systèmes embarqués sont en retard.

2️⃣ La directive NIS2 et le Cyber Resilience Act européen entrent
en application. Les opérateurs d'infrastructures critiques (énergie,
transport, télécoms) doivent prouver la sécurité de leurs équipements
autonomes — y compris contre des menaces futures.

3️⃣ La souveraineté européenne sur les composants critiques redevient
un enjeu stratégique. Trop de drones civils et défense reposent
aujourd'hui sur des piles logicielles non auditables.

HESIA : firmware sécurisé + attestation matérielle + cryptographie
post-quantique standardisée. Conçu en France. Pour la conformité
européenne.

→ hesia.eu

#PostQuantum #NIS2 #DroneSecurity #Souveraineté
```

---

**Post #2 — Pédagogie ML-KEM (jour 3)**

```
ML-KEM-1024 remplace RSA et ECDH. Voilà ce que ça change concrètement
pour un drone.

🔹 RSA-2048 (aujourd'hui) : cassable par un ordinateur quantique
   suffisamment grand. Calendrier ANSSI : "menace réaliste à
   horizon 10-15 ans".

🔹 ECDH (X25519) : même problème. Algorithme de Shor.

🔹 ML-KEM-1024 (NIST FIPS 203) : standard post-quantique. Sécurité
   estimée 256 bits classique + résistance Shor.

Le coût ? Une clé publique ML-KEM-1024 fait 1568 octets, contre 32
pour X25519. Le ciphertext est ~1568 octets aussi. Pour un échange
TLS, on parle d'un overhead réseau de quelques kilo-octets — invisible
sur LTE/SatCom modernes.

Le vrai défi n'est pas le calcul. C'est l'intégration : key store,
attestation, gestion de session, rotation. C'est là qu'on travaille.

🛠 Implémentation HESIA : ML-KEM-1024 hybridé avec X25519 (défense en
profondeur), session re-key < 60s, scellement OP-TEE de la clé maître.

#PostQuantum #MLKEM #Cryptography #EmbeddedSecurity
```

---

**Post #3 — News commentée (jour 7)**

```
[Quand un événement public arrive — exemple type :]

L'ANSSI vient de publier sa nouvelle note technique sur la migration
post-quantique. 3 points qui devraient mobiliser tous les opérateurs
de systèmes embarqués critiques :

1. Les algorithmes acceptés sont désormais explicitement nommés :
   ML-KEM, ML-DSA, SLH-DSA. Pas d'ambiguïté.

2. La notion de "harvest now, decrypt later" est officiellement
   reconnue comme un risque actuel — pas futur.

3. Le calendrier "horizon 2030" pour la migration des systèmes
   sensibles est confirmé. Cela laisse 4 ans pour les cycles
   de qualification longs.

Pour les fabricants de drones, équipements industriels, capteurs IoT
critique : la fenêtre de remplacement matériel s'ouvre maintenant.

#ANSSI #PostQuantum #Cybersecurity
```

---

**Post #4 — Use case inspection énergie (jour 10)**

```
Un opérateur de réseau de transport d'électricité opère 150 drones
d'inspection. Que se passe-t-il si un attaquant prend le contrôle
de la flotte ?

Trois scénarios concrets :
🔻 Vol des données d'inspection (cartographie fine du réseau,
   points de vulnérabilité physique).
🔻 Injection de fausses inspections (un défaut critique signalé
   comme conforme).
🔻 Détournement physique (collision, atterrissage forcé sur
   poste électrique).

L'inspection drone est un nouveau vecteur d'attaque sur les
infrastructures critiques.

La directive NIS2 (annexe I.5) impose la sécurisation des "systèmes
de contrôle et d'acquisition de données". Les drones rentrent dans
ce périmètre dès qu'ils contribuent à la décision de maintenance.

Notre approche HESIA :
✓ Authentification mutuelle drone↔sol (TLS 1.3 mTLS + post-quantique)
✓ Signature des photos / vidéos d'inspection (ancrage Ed25519)
✓ Attestation matérielle au boot (rejette le firmware non signé)
✓ Journal d'audit immuable transmis au SIEM client

#NIS2 #CriticalInfrastructure #DroneInspection #Cybersecurity
```

---

**Post #5 — Tribune souveraineté (jour 14)**

```
"Souveraineté numérique" est devenu un slogan. Voici ce qu'elle
signifie concrètement pour un drone européen :

1. Code source auditable par l'opérateur (pas de boîte noire).
2. Hébergement et chiffrement sur sol européen (RGPD + NIS2).
3. Chaîne d'approvisionnement composants tracée.
4. Pas de backdoor légale étrangère (CLOUD Act, FISA 702).
5. Capacité à débrancher / patcher sans dépendance extra-UE.

Aujourd'hui, beaucoup d'acteurs cochent 1 ou 2 cases. Quasi aucun
les coche toutes les 5.

C'est notre cahier des charges chez HESIA — pas un argumentaire
marketing.

Ça implique des choix techniques engageants :
→ Stack OS Linux maintenu sur serveurs UE
→ Bibliothèques crypto open source vérifiées (liboqs, OpenSSL)
→ TEE via OP-TEE (libre) plutôt que solutions propriétaires fermées
→ Hardware Jetson (NVIDIA) → migration prévue vers SoC européen
   quand maturité suffisante (Kalray, GreenWaves, CEA-Leti)

Souveraineté = inconfort technique assumé, pas claim marketing.

#Souveraineté #EuropeanTech #Cybersecurity
```

---

**Post #6 — Annonce produit (jour 17)**

```
HESIA Core 1.0 — disponible en préversion technique.

Ce qui est fait :
✅ Firmware durci avec sandboxing seccomp/AppArmor
✅ Canal sécurisé ML-KEM-1024 + X25519 hybride
✅ AES-256-GCM avec rotation de clé < 60s
✅ Signature Ed25519 des policies opérateur
✅ Allowlist signée pour modules tiers
✅ Sandbox vidéo/CV avec limites mémoire et temps
✅ Audit log immuable (SQLite + chaînage hash)

Ce qui est en cours :
🟡 OP-TEE TA pour scellement matériel (Q3 2026)
🟡 Attestation à distance avec binding HW (Q4 2026)
🟡 Déploiement RPMB sur cible eMMC (Q1 2027)

Ce qu'on ne prétend pas :
❌ Certification CSPN (en cours d'instruction)
❌ Certification Common Criteria EAL4+ (roadmap 2027)
❌ Production à l'échelle (TRL 4-5 aujourd'hui)

Notre roadmap est publique. Si vous opérez un cas d'usage concret
sur drone d'inspection, surveillance ou défense, nous cherchons
3 partenaires pilotes pour Q3 2026.

→ hesia.eu/contact

#ProductLaunch #EmbeddedSecurity #PostQuantum
```

---

**Post #7 — Décryptage CRA (jour 21)**

```
Le Cyber Resilience Act européen entre en application progressive
jusqu'en 2027. Pour qui fabrique du logiciel embarqué, voilà les
4 obligations qui changent la donne :

1️⃣ Sécurité par conception (Article 13)
   → Pas de credentials par défaut, pas de surface d'attaque
   inutile, principe du moindre privilège imposé.

2️⃣ Mise à jour de sécurité (Article 14)
   → Patches gratuits pendant la durée de support déclarée
   (5 ans minimum recommandé pour matériel critique).

3️⃣ Notification d'incident (Article 11)
   → 24h pour signaler une vulnérabilité activement exploitée
   à l'ENISA. 72h pour notifier les utilisateurs.

4️⃣ Auto-évaluation ou tiers de confiance (Article 24)
   → Pour les "produits importants" et "critiques" (catégorie
   incluant les systèmes de contrôle industriels et certains
   drones), audit par organisme notifié obligatoire.

Notre take : CRA est probablement l'évolution réglementaire la
plus structurante depuis le RGPD pour les fabricants. Anticiper
maintenant coûte 10x moins cher que rattraper en 2027.

#CyberResilienceAct #CRA #EUCompliance
```

---

**Post #8 — Recrutement / équipe (jour 28)**

```
On recrute un Senior Security Engineer (Embedded / TEE).

Mission : porter HESIA Core sur OP-TEE Trusted Application,
implémenter le scellement matériel sur Jetson Orin et iMX9, et
préparer la certification CSPN.

Stack : C/C++, OP-TEE, ARM TrustZone, Yocto, Linux kernel.
Bonus : expérience sur Common Criteria, FIPS 140-3, ou
post-quantum cryptography.

Ce qu'on offre :
- Travail sur un sujet rare et techniquement profond
- Code source ouvert aux audits (engagement avec nos clients)
- Salaire 75-95k€ selon profil + BSPCE
- Hybride Paris / full remote (UE)
- Pas de "stand-up obligatoire" ni de "scrum tyrannique"

Si vous avez déjà débuggé une faute d'alignement dans une TA, ou
si vous savez ce qu'est un "exporter binding" RFC 9266, on devrait
se parler.

📧 jobs@hesia.eu — pas de recruteurs externes svp.

#Hiring #SecurityEngineering #OPTEE
```

---

**Post #9 — Veille concurrence (jour 35)**

```
Le marché de la cybersécurité drone se structure. État des lieux
sans complaisance :

🇺🇸 Skydio Defense : excellente plateforme matérielle, mais ITAR
   restrictif et dépendance US assumée. Hors-jeu pour beaucoup
   d'acheteurs européens souverains.

🇺🇸 Anduril : plateforme défense intégrée, prix élevé, focus
   contrats US-DoD. Difficilement adressable hors alliances
   bilatérales.

🇫🇷 Parrot : matériel français reconnu, sécurité crypto en
   progrès, mais positionnement défense/dual-use historiquement
   secondaire vs. grand public et pro.

🇩🇪 Quantum Systems : qualité matérielle, focus reconnaissance
   tactique. Moins de communication publique sur la stack crypto.

🇮🇱 Elbit / IAI : offre défense intégrée mature, mais
   souveraineté européenne difficile à argumenter selon
   les acheteurs.

🇫🇷 HESIA (nous) : pas de matériel propriétaire. On fournit la
   couche logicielle de sécurité que ces acteurs (et d'autres)
   peuvent intégrer ou que les opérateurs peuvent embarquer sur
   leur flotte hétérogène. Notre concurrent direct est plutôt
   le "fait-maison" interne.

Le marché n'a pas encore son "Wiz" ou son "CrowdStrike". Il a
besoin d'une couche logicielle horizontale, certifiée, souveraine.

#DroneSecurity #CompetitiveLandscape
```

---

**Post #10 — Tribune fondateur (jour 42)**

```
J'ai passé 6 mois à étudier les rapports d'incidents drones
publics. Ce que j'en retire :

→ 80% des incidents critiques ne sont PAS des cyberattaques
exotiques. Ce sont des credentials par défaut, des firmwares
non signés, des canaux radio en clair, des protocoles MAVLink
non authentifiés.

→ La cyber-sécurité drone, en 2026, ressemble à la cyber-sécurité
serveur en 2005. On débute. Les hardening basiques ne sont pas
en place sur la majorité du parc.

→ Le post-quantum est important pour l'avenir, mais le danger
quotidien c'est le présent. Un drone qui boot un firmware non
signé est aujourd'hui plus vulnérable qu'un drone vulnérable à
Shor en 2035.

C'est pour ça que HESIA travaille sur les deux fronts en parallèle :

1. Hardening immédiat (boot signé, mTLS, allowlist, sandbox).
2. Migration post-quantique (ML-KEM hybridé, ML-DSA pour les
   politiques).

Pas l'un OU l'autre. Les deux. C'est non-négociable pour une
flotte critique.

#DroneSecurity #ThoughtLeadership #Cybersecurity
```

---

**Post #11 — Webinar invitation (jour 49)**

```
Webinar le [date] : "Migrer une flotte de drones vers la
cryptographie post-quantique — guide opérationnel".

Au programme (45 min + Q&A) :
🔸 Pourquoi maintenant et pas dans 5 ans
🔸 Cartographie des algorithmes (NIST FIPS 203/204/205)
🔸 Architecture hybride : éviter le "tout post-quantique" prématuré
🔸 Coût mémoire / réseau / CPU sur cibles embarquées
   (Jetson, iMX, STM32)
🔸 Plan de migration en 4 étapes pour une flotte de 50-500 drones
🔸 Erreurs fréquentes (mauvais binding, oubli d'attestation)

Pour qui : RSSI, architectes sécurité, responsables technique de
flottes UAS, équipes innovation.

Inscription : hesia.eu/webinars
Replay envoyé aux inscrits.

🇫🇷 Webinar en français. Une session anglaise sera planifiée selon
demande.

#PostQuantum #Webinar #DroneSecurity
```

---

**Post #12 — Bilan trimestre (jour 56)**

```
Bilan T1 chez HESIA. Ce qu'on a fait, ce qui a marché, ce qui
a échoué.

✅ Ce qui a marché :
→ Lancement site + 4500 visiteurs uniques sur 90 jours.
→ 3 RDV avec acheteurs OIV qualifiés (énergie, transport).
→ 1ère POC signée avec un opérateur d'inspection éolien.
→ Audit externe de la stack crypto par [cabinet] — rapport
  publié sous embargo court.
→ +850 followers LinkedIn.

🟡 Mitigé :
→ Nos campagnes LinkedIn Ads sur les CISO grands comptes : CPL
  élevé (220€), conversion correcte mais cycle long. À ajuster.
→ Le webinar a attiré beaucoup d'inscrits hors-cible (curieux
  techniques) — utile mais pas commercialement.

❌ Ce qui a raté :
→ Notre tentative de salon "FIC light" : peu de prospects
  défense souveraine présents. Mauvais ciblage de notre part.
→ Article de blog sur les benchmarks ML-DSA : techniquement
  bon, mais publié à un mauvais moment (vacances août). Reach
  faible.

Roadmap T2 :
🔸 Sortie HESIA Core 1.1 avec OP-TEE TA en alpha
🔸 1er livre blanc public : "PQC pour systèmes embarqués"
🔸 Ouverture POC #2 (cible défense)

Transparence > effet d'annonce.

#StartupLife #Cybersecurity #Transparency
```

---

### 2.5 Format et règles d'écriture LinkedIn

- **Longueur** : posts entre 800 et 1500 caractères. Au-dessus, ajouter un PDF carrousel ou un lien blog.
- **Hooks** (3 premières lignes visibles avant "voir plus") : posent une tension, une statistique, ou une question concrète. Pas de "I'm thrilled to announce".
- **Émojis** : utilisés avec parcimonie (1-3 par post max), comme balises visuelles, jamais décoratifs gratuits. ✅ ❌ 🟡 🔻 → ✓ acceptables.
- **Hashtags** : 3 à 5 max, spécialisés (#PostQuantum #NIS2 #DroneSecurity), pas de #motivation ou #Monday.
- **Mention** : citer les sources externes (NIST, ANSSI, Commission EU, chercheurs nommés) augmente la crédibilité et le reach.
- **Pas de** : memes, citations Steve Jobs, "8 leçons du fondateur de…", carrousels de productivité.

### 2.6 Règles de modération et d'engagement

- Répondre à **tous les commentaires pertinents** dans les 24h.
- Pour les commentaires hostiles ou trolls : 1 réponse factuelle, pas plus. Ne jamais escalader.
- Pour les commentaires de pairs techniques : approfondir, citer des sources, accepter d'avoir tort en public si c'est le cas.
- Pour les concurrents : pas d'attaque directe. Si comparaison nécessaire, factuelle et publique.

## 3. GitHub (canal P0)

### 3.1 Profil organisation

**Nom** : `hesia-security` (ou `hesia-eu` si pris)
**Bio** : `Sovereign post-quantum security for autonomous systems. Made in 🇫🇷.`
**Site** : hesia.eu
**Email** : opensource@hesia.eu

**README organisation** :
```markdown
# HESIA — Open Engineering

We build sovereign post-quantum security software for drones and
autonomous systems.

While our core product is proprietary, we maintain and contribute
to several open-source components and publish technical research
publicly.

## Public repositories

- `hesia-pqc-bench/` — Benchmarks ML-KEM / ML-DSA on embedded targets.
- `optee-hesia-skeleton/` — Reference OP-TEE TA skeleton (MIT).
- `pqc-migration-guide/` — Practical migration patterns (CC-BY-SA).

## Contributing

We accept PRs on the public repos above. For security disclosures,
please write to security@hesia.eu (PGP key on website).

## Contact

📧 hello@hesia.eu — 🌐 hesia.eu
```

### 3.2 Stratégie repos publics

3 dépôts à publier dans les 6 premiers mois :

1. **`hesia-pqc-bench`** : benchmarks reproductibles ML-KEM / ML-DSA / Falcon sur Jetson Orin, iMX9, STM32. Donne immédiatement de la crédibilité technique. Source de citations et de leads inbound.

2. **`optee-ta-skeleton`** : squelette d'application TEE OP-TEE avec build Yocto reproductible, MIT-licensed. Très peu d'exemples publics propres existent — opportunité de SEO long-tail.

3. **`pqc-migration-guide`** : guide écrit (markdown), checklists, snippets, sous CC-BY-SA. Devient une référence linkable depuis blog et LinkedIn.

**Ne pas publier** : le code core HESIA. Mais documenter publiquement les *interfaces* et les *protocoles* (specs, schémas) pour audit communautaire.

### 3.3 SECURITY.md type

```markdown
# Security Policy

## Reporting a vulnerability

If you discover a security vulnerability in any HESIA product or
public repo, please email security@hesia.eu with:

- Description of the vulnerability and potential impact
- Steps to reproduce
- Affected versions

PGP key: [fingerprint] — full key at hesia.eu/security/pgp.asc.

We commit to:
- Acknowledge receipt within 48 hours
- Provide an initial assessment within 7 days
- Maintain confidentiality during fix development
- Credit reporters in advisories (unless anonymity requested)

## Bug bounty

A formal bug bounty program is planned for late 2026. In the
interim, security researchers contributing material findings may
receive monetary recognition on a case-by-case basis.

## Coordinated disclosure

We follow a 90-day responsible disclosure window from acknowledgment
to public advisory, extendable in good-faith negotiation.
```

## 4. X / Twitter

**Handle** : `@hesia_security` (ou `@hesia_eu`)
**Bio** :
```
Sovereign post-quantum security for autonomous systems.
ML-KEM-1024 · OP-TEE · NIS2 · CRA · 🇫🇷 → 🇪🇺
hesia.eu
```
**Header** : même bannière que LinkedIn mais format 1500x500.

**Stratégie de contenu** :
- **40%** : reposts techniques (papiers de recherche, advisories, ANSSI/CISA).
- **30%** : commentaires courts (200-280 caractères) sur l'actu cyber/PQC.
- **20%** : threads techniques (5-15 tweets) sur sujets de fond.
- **10%** : annonces produit / équipe.

**Communauté à suivre/engager** : @ANSSI_FR, @ENISA_EU, @NIST, @SchneierBlog, @matthew_d_green, @msuiche, @FredRaynal, chercheurs INRIA, CryptoExperts, Quarkslab.

**Pas de** : threads "10 lessons", "I just made $1M", emoji spam.

## 5. YouTube

**Nom chaîne** : `HESIA Security`
**Bannière** : cohérente brand.
**Description** :
```
HESIA designs sovereign post-quantum security for drones and
autonomous systems.

This channel hosts:
→ Technical deep-dives (cryptography, embedded systems, OP-TEE)
→ Webinar replays
→ Product walkthroughs
→ Conference talks

For business inquiries: hello@hesia.eu
```

**Plan éditorial 12 mois** :
| Mois | Vidéo |
|------|-------|
| M+1 | Présentation HESIA (3 min) — pour intégration sur site |
| M+2 | Replay webinar #1 PQC migration |
| M+3 | Tutorial : compiler liboqs sur Jetson Orin |
| M+4 | Interview fondateur — pourquoi HESIA |
| M+5 | Replay webinar #2 NIS2 |
| M+6 | Demo : signature Ed25519 d'une policy opérateur |
| M+8 | Talk conférence (FIC ou équivalent) |
| M+10 | Replay webinar #3 OP-TEE & secure boot |
| M+12 | Bilan année + roadmap |

**Production minimum viable** :
- Caméra : Sony ZV-E10 ou équivalent (≈ 800€).
- Micro : Rode NT-USB ou Shure MV7 (≈ 250€).
- Lumière : 2 panneaux LED softbox (≈ 200€).
- Logiciel : DaVinci Resolve (gratuit), CapCut Pro pour montage rapide.
- Local : pièce avec mur uni, fond sobre, pas de drone décoratif accroché au mur (cliché).

## 6. Bluesky

**Handle** : `@hesia.bsky.social` (ou domaine custom `@hesia.eu`)
**Stratégie** : version cyber-pure de X. Communauté infosec et chercheurs en migration depuis Twitter. Effort faible mais bonne signal/noise ratio.

**Posts** : reposter 70% du contenu X, ajouter quelques commentaires originaux sur les papiers de recherche.

## 7. Mastodon (infosec.exchange)

Compte uniquement si un membre de l'équipe est déjà actif personnellement. Sinon, ne pas créer pour éviter compte fantôme.

## 8. KPI réseaux sociaux (12 mois)

| Métrique | T+3 mois | T+6 mois | T+12 mois |
|----------|----------|----------|-----------|
| Followers LinkedIn page | 250 | 800 | 2000 |
| Followers LinkedIn perso (fondateur 1) | 1200 | 2500 | 5000 |
| Vues moyennes par post | 800 | 2000 | 5000 |
| Engagement rate moyen | 2-3% | 3-4% | 4-5% |
| Inbound leads via LinkedIn | 1-2/mois | 5/mois | 10/mois |
| Stars GitHub repo principal | 50 | 200 | 500 |
| Subscribers YouTube | 100 | 400 | 1500 |

Ces chiffres sont des **objectifs réalistes** pour un acteur deeptech B2B avec budget marketing modéré (60-120k€/an). Les viser sans drogues stimulantes ni achats de followers.

## 9. Outils recommandés

| Outil | Usage | Prix indicatif |
|-------|-------|----------------|
| Buffer ou Hypefury | Programmation posts X / LinkedIn | 15-50€/mois |
| Shield Analytics | Analytics LinkedIn perso | 10€/mois |
| Notion ou Airtable | Calendrier éditorial | 0-20€/mois |
| Canva Pro | Visuels rapides | 12€/mois |
| Figma | Visuels avancés / charte | 15€/mois |
| OBS Studio | Enregistrement vidéo / live | 0€ |
| DaVinci Resolve | Montage vidéo | 0€ |

**Ne pas utiliser** : tout outil qui "écrit vos posts LinkedIn avec l'IA". Le premier post rédigé par GPT sans relecture humaine fini en capture d'écran ironique sur Twitter.

## 10. Antifragiles : ce qu'on ne fait jamais

- ❌ Acheter des followers / engagement.
- ❌ Pods d'engagement (groupes de likes croisés).
- ❌ Commentaires copiés-collés "Great post! 🚀".
- ❌ Citer des concurrents pour les attaquer.
- ❌ Surcommuniquer sur des fonctionnalités non développées.
- ❌ Promettre des dates de certification non confirmées par l'organisme.
- ❌ Diffuser des photos/captures d'opérations clients sans accord écrit.
- ❌ Tweeter sous le coup de l'émotion politique.

---

**Dernière mise à jour** : 2026-04-25
**Maintenu par** : Marketing & Communication HESIA
