# 06 — Content Marketing & SEO

> Plan éditorial complet : livres blancs, blog, webinaires, podcast, SEO. Le content est le moteur principal de la crédibilité technique et du lead inbound. Effort dominant (40% du budget marketing) car c'est l'avantage compétitif durable.

## 1. Pourquoi le content avant tout

L'audience cible (CISO, RSSI, ingénieurs sécurité, acheteurs défense) consomme :
- Des livres blancs techniques (référence, partage interne dans leurs équipes).
- Des articles longs sur les blogs spécialisés (LeMagIT, Bleeping Computer).
- Des conférences techniques (SSTIC, USENIX, Real World Crypto).
- Des podcasts "lourds" (Risky Business, NoLimitSecu).

Elle **détecte** :
- Le ghostwriting d'agence (vocabulaire vague, recyclage de buzzwords).
- Le contenu généré par LLM sans relecture (formulations interchangeables).
- Le bullshit "AI-powered next-gen blockchain quantum-resistant".

Conséquence : tout le contenu HESIA est **rédigé ou validé par un ingénieur technique**, jamais par un copywriter pur. Le ton est sobre, précis, sourcé.

## 2. Piliers thématiques (5 piliers)

Toute publication HESIA s'inscrit dans l'un de ces 5 piliers. Cohérence + SEO + autorité.

| # | Pilier | Mots-clés primaires | Volume contenu/an |
|---|--------|---------------------|-------------------|
| 1 | **Post-Quantum Cryptography pour systèmes embarqués** | ml-kem, ml-dsa, falcon, hybride, migration pqc | 12 articles + 1 livre blanc |
| 2 | **Sécurité drones / UAS** | drone hardening, secure boot, mavlink security, uas cybersecurity | 10 articles |
| 3 | **OP-TEE & TEE pour l'embarqué** | optee, trustzone, secure boot jetson, attestation matérielle | 8 articles + 1 guide pratique |
| 4 | **Conformité réglementaire** (NIS2, CRA, ANSSI, FIPS) | nis2 systèmes embarqués, cra obligations, anssi homologation | 8 articles + 1 matrice |
| 5 | **Souveraineté tech européenne** | souveraineté numérique, cloud act, made in europe, ITAR | 6 articles |

## 3. Livres blancs (whitepapers)

### 3.1 Calendrier 12 mois

| Trimestre | Titre | Pages | Audience |
|-----------|-------|-------|----------|
| T1 | **Migrer une flotte de drones vers la cryptographie post-quantique** | 24 | RSSI, dir. technique, ingés sécu |
| T2 | **NIS2 et UAS : matrice de conformité opérationnelle** | 18 | RSSI OIV, conformité |
| T3 | **OP-TEE pour drones critiques : guide d'implémentation** | 32 | Ingénieurs sécurité, architectes |
| T4 | **Souveraineté de la cyber-sécurité embarquée européenne — état des lieux 2027** | 40 | DSI, dir. innovation, décideurs publics |

### 3.2 Anatomie d'un livre blanc HESIA

```
1. Couverture brand (1p)
2. Abstract / Executive summary (1p — 5 bullet points)
3. Sommaire (1p)
4. Contexte et enjeux (2-4p)
5. Cadre technique / réglementaire (2-4p)
6. Analyse approfondie (8-15p, le cœur)
7. Recommandations actionnables (2-4p)
8. Conclusion (1p)
9. Références bibliographiques (1-2p)
10. À propos de HESIA (0.5p)
11. Contact (0.5p)
```

**Règles** :
- Tous les chiffres, sources, normes citées et datées.
- Schémas vectoriels (pas de capture d'écran flou).
- Pas de logo HESIA toutes les 2 pages — discrétion.
- Téléchargement : email + entreprise + titre obligatoires (gating modéré). Pas de téléphone (friction trop élevée).
- Format : PDF + version web (HTML lisible mobile).

### 3.3 Livre blanc T1 — détail rédactionnel

**Titre** : *Migrer une flotte de drones vers la cryptographie post-quantique — Guide opérationnel*

**Abstract** :
> En août 2024, le NIST a publié les premières normes post-quantiques (FIPS 203, 204, 205). L'ANSSI, le BSI et la NSA imposent désormais des roadmaps de migration pour les systèmes critiques. Pour les opérateurs de flottes de drones d'inspection ou de défense, la fenêtre d'action s'ouvre maintenant — pas en 2030. Ce livre blanc fournit une cartographie technique des algorithmes (ML-KEM, ML-DSA, SLH-DSA), un plan de migration en 4 étapes, des benchmarks réels sur cibles embarquées (Jetson Orin, iMX9, STM32), et une checklist d'audit applicable immédiatement.

**Sommaire** :
1. Pourquoi PQC maintenant pour l'embarqué (3p)
2. Cartographie des algorithmes NIST (4p)
3. Stratégie hybride : combiner classique et post-quantique (3p)
4. Benchmarks sur cibles embarquées (5p)
5. Plan de migration en 4 étapes (4p)
6. Erreurs fréquentes à éviter (2p)
7. Checklist d'audit (1p)
8. Conclusion + ressources (1p)

**Diffusion** :
- Page dédiée /resources/pqc-migration-guide
- Email blast à la base inscrits newsletter
- 4 posts LinkedIn cibles différents (CISO, ingés, dir. innovation, défense)
- Document Ad LinkedIn campagne payante 4 semaines
- Tribune presse spé adaptée (LeMagIT, ZDNet)
- Webinar associé en T1 (replay sur YouTube)

## 4. Blog editorial

### 4.1 Stratégie de blog

- **Hébergement** : sous-domaine `blog.hesia.eu` ou path `/blog/`. Path préféré (juice SEO sur domaine racine).
- **Publication** : 2 articles/mois fixes (24/an). Pas plus, sinon perte de qualité.
- **Longueur cible** : 1500-3500 mots pour articles fondamentaux ; 800-1500 pour news commentées.
- **Auteur visible** : nom + photo + courte bio + lien LinkedIn. Pas d'auteur "HESIA Team" anonyme.
- **CTAs** : 1 CTA discret en milieu d'article (link vers livre blanc related), 1 CTA en fin (newsletter ou demo).

### 4.2 Calendrier éditorial 12 mois (24 articles)

**T1 — Trimestre PQC dominant**
1. *Pourquoi ML-KEM-1024 et pas ML-KEM-512 pour les drones* (technique, 2500 mots)
2. *Comprendre le "harvest now, decrypt later" en 5 minutes* (pédago, 1200 mots)
3. *Benchmark ML-DSA-87 sur Jetson Orin Nano* (technique pur, 2000 mots)
4. *NIS2 : ce qui change pour les fabricants UAS au 1er janvier 2027* (réglementaire, 1800 mots)
5. *L'erreur classique en migration PQC : oublier le binding d'attestation* (retour d'expérience, 1500 mots)
6. *Que dit l'ANSSI dans son guide post-quantique 2025 — décryptage* (analyse, 2200 mots)

**T2 — Trimestre OP-TEE / TEE**
7. *Compiler liboqs sur OP-TEE — récit de portage* (technique avancé, 3500 mots)
8. *Secure boot sur Jetson Orin : l'état réel du fuse-binding* (recherche, 2800 mots)
9. *Pourquoi RPMB n'est pas optionnel pour les flottes critiques* (architectural, 2000 mots)
10. *Common Criteria EAL4+ : que coûte vraiment la certification ?* (business, 1500 mots)
11. *Cyber Resilience Act : checklist en 27 points pour fabricants embarqués* (réglementaire, 2500 mots)
12. *Notre approche du sandboxing kernel sur Linux embarqué* (technique, 2800 mots)

**T3 — Trimestre Drone Security**
13. *MAVLink en clair — combien de drones européens en sont encore là ?* (analyse marché, 1800 mots)
14. *Anatomie d'une attaque par injection de policy non signée* (technique, 2200 mots)
15. *Audit interne d'une flotte de drones — méthode en 7 étapes* (méthodologique, 2500 mots)
16. *Drones d'inspection éolien : 3 vecteurs d'attaque que personne ne couvre* (use case, 1800 mots)
17. *Que pense l'ANSSI des UAS opérés par des collectivités ?* (analyse, 2000 mots)
18. *Pourquoi le firmware signé n'est pas suffisant si le secure boot ne l'est pas* (technique, 1800 mots)

**T4 — Trimestre stratégie / souveraineté**
19. *Souveraineté numérique européenne : 5 critères techniques mesurables* (opinion, 2200 mots)
20. *ITAR appliqué aux drones — les pièges pour les acheteurs européens* (juridique/business, 2500 mots)
21. *Bilan PQC 2026 : où en sont vraiment les agences européennes* (analyse, 2800 mots)
22. *Ce qu'on a appris en construisant HESIA — 12 mois de retours* (récit, 2000 mots)
23. *Roadmap 2027 — ce qu'on prépare et pourquoi* (annonce, 1800 mots)
24. *Top 10 des publications HESIA 2026 — récap annuel* (curation, 1500 mots)

### 4.3 Format type d'un article blog

```markdown
# Titre clair, riche en mot-clé primaire (60-65 caractères)

Date : 2026-XX-XX · Lecture : ~XX min · Auteur : [Nom] [LinkedIn]

> Résumé en 2-3 phrases (placé en exergue) : ce que le lecteur saura
> après lecture.

## Le problème

[300-500 mots — pourquoi le sujet importe maintenant. Une stat, une
référence ANSSI/NIST, un cas concret.]

## Analyse technique

[1000-2000 mots — schémas, tableaux, code blocks. Sources citées
inline.]

## Implications opérationnelles

[400-700 mots — pour qui ça change quoi. Pas de buzzwords.]

## Erreurs fréquentes

[3-5 erreurs réelles, chacune avec contre-mesure.]

## Conclusion

[100-200 mots — synthèse + une question ouverte si pertinent.]

---

**Sources & références**
[liste à puces sourcée]

**Pour aller plus loin**
- Livre blanc HESIA : [titre] (CTA discret)
- Article connexe : [titre]
```

### 4.4 SEO technique

**On-page** :
- Title tag : 50-60 caractères, mot-clé primaire en début.
- Meta description : 150-160 caractères, vendre le clic, pas le SEO.
- H1 unique = titre article. H2/H3 hiérarchisés.
- URL : `/blog/[slug-court-keyword]` — pas de date dans l'URL.
- Image hero : WebP, alt sémantique, lazy loading.
- Schéma JSON-LD : `BlogPosting` + `Article`.
- Canonical URL : explicite.
- Liens internes : 3-5 par article vers autres articles ou pages produit.
- Liens externes : 2-4 par article vers sources autoritaires (NIST, ANSSI, IETF).

**Vitesse** :
- Lighthouse score >= 95 mobile et desktop.
- Pas de tracker tiers lourd. Plausible OK.
- Images < 200 Ko (WebP).
- Pas de carrousel JS bloquant.

**Maillage** :
- Page pillar par pilier thématique (`/topic/post-quantum-cryptography/`) avec liens vers tous les articles du pilier.
- Topical authority sur 5 piliers > essayer 50 sujets.

### 4.5 Mots-clés cibles SEO (priorité)

**FR — volume mensuel estimé / difficulté Ahrefs** :
| Mot-clé | Volume | Difficulté |
|---------|--------|------------|
| cryptographie post-quantique | 1 200 | 35 |
| ml-kem | 380 | 28 |
| ml-dsa | 220 | 25 |
| nis2 directive | 6 200 | 45 |
| cyber resilience act | 1 900 | 40 |
| sécurité drone | 880 | 38 |
| drone industriel sécurité | 320 | 30 |
| op-tee tutoriel | 110 | 22 |
| secure boot jetson | 240 | 28 |
| attestation matérielle | 290 | 32 |
| anssi cspn | 480 | 35 |
| common criteria eal4 | 590 | 38 |

**EN** :
| Mot-clé | Volume | Difficulté |
|---------|--------|------------|
| post-quantum cryptography | 5 400 | 50 |
| ml-kem implementation | 480 | 32 |
| drone cybersecurity | 1 600 | 45 |
| op-tee tutorial | 590 | 28 |
| nis2 directive | 14 800 | 55 |
| cyber resilience act | 5 800 | 50 |

Cibler en priorité les longues traînes (volume bas + difficulté basse). Ex : "ml-kem benchmark jetson", "op-tee yocto build", "harvest now decrypt later drones".

## 5. Webinaires

### 5.1 Programme 12 mois (4 webinaires)

| Trimestre | Titre | Format | Durée | Audience cible |
|-----------|-------|--------|-------|----------------|
| T1 | Migrer une flotte UAS vers PQC — guide opérationnel | Solo fondateur + Q&A | 60 min | Tech / RSSI |
| T2 | NIS2 + drones : décrypter les obligations qui s'appliquent | Solo + invité juriste | 60 min | Conformité / RSSI |
| T3 | OP-TEE en pratique : porter une appli sur Jetson Orin | Démo live + Q&A | 75 min | Ingénieurs |
| T4 | Souveraineté cyber : panorama des composants critiques | Panel 3 invités | 90 min | DSI / décideurs |

### 5.2 Stack webinar

- **Plateforme** : Livestorm (FR, RGPD-friendly) ou Demio. Pas Zoom Webinar (UI dépassée + lock-in).
- **Inscription** : page dédiée avec form court (email + nom + entreprise + titre).
- **Replay** : automatique → email post-event aux inscrits + page replay publique 30j après.
- **Durée** : 45-75 min selon format. Q&A 15 min séparé.
- **Production** : ring light + USB micro + caméra mirrorless. Pas d'agence vidéo extérieure.

### 5.3 Promotion d'un webinar

**J-30** : annonce site + LinkedIn page + email base + posts personnels fondateurs.
**J-21** : LinkedIn Ads campagne 1500-3000€ ciblée audiences pertinentes.
**J-14** : article blog associé (teaser + sommaire détaillé).
**J-7** : email reminder à inscrits non-attended.
**J-3** : LinkedIn post fondateur + reminder.
**J-1** : email + LinkedIn message direct ouvert.
**J0** : live + thank you email.
**J+1** : email replay.
**J+7** : email follow-up avec ressources complémentaires + CTA demo.

### 5.4 KPI webinar

| KPI | Cible |
|-----|-------|
| Inscrits par webinar | 200-500 |
| Show-up rate | 35-50% |
| MQL générés | 30-80 par webinar |
| Demo requests post-webinar | 5-15 |
| NPS post-webinar | > 40 |

## 6. Podcast HESIA (option à confirmer en T2)

### 6.1 Concept

**Titre de travail** : *Edge Defense — la sécurité des systèmes autonomes*

**Format** : entretien 45-60 min avec un expert (ingénieur sécurité, chercheur, acheteur défense, RSSI). Sujets profonds, sans pub, sans hype.

**Cadence** : bi-mensuel (24 épisodes/an si lancé).

**Animateur** : fondateur HESIA (connaissance du sujet > voix radio).

### 6.2 Décision T2

Lancement conditionnel à :
- Disponibilité 8h/semaine équipe pour produire (production + montage + promotion).
- 5 invités confirmés sur les 5 premiers épisodes.
- Stack technique testé (Riverside.fm + Hindenburg / Descript).

Si pas de lancement : sponsoring de podcasts existants (cf. `05_CAMPAGNES_PUBLICITAIRES.md`).

### 6.3 Si lancement

**Épisode 1** : *Le dossier post-quantique — où en sommes-nous vraiment ?*
**Épisode 2** : *Drones d'inspection critique : le retour d'expérience d'un RSSI*
**Épisode 3** : *Souveraineté technologique européenne : illusion ou levier réel ?*

**Distribution** : Spotify, Apple Podcasts, Overcast, Pocket Casts, RSS public, page hesia.eu/podcast.

## 7. Newsletter

### 7.1 Format

- **Cadence** : bi-mensuelle (2 envois/mois) → annuelle (24 numéros/an).
- **Outil** : Buttondown ou ConvertKit (RGPD-friendly + simples). Pas Mailchimp (UX vieillit, hors-EU).
- **Format** : email texte simple + 1-2 visuels max. Pas de design "magazine".
- **Longueur** : 700-1500 mots.

### 7.2 Structure type

```
HESIA #07 — Le post-quantique en 5 actualités

Bonjour [Prénom],

Voici les nouvelles importantes dans la cyber-sécurité embarquée
sur les 15 derniers jours :

1. **NIST publie SP 800-208 final** — implications pour les
   signatures stateful sur drones. [Lien + commentaire 80 mots]

2. **ANSSI met à jour son guide cryptographique** — 3 changements
   à connaître. [Lien + commentaire]

3. **Skydio annonce son passage à ML-KEM** — analyse comparative
   de leur approche. [Lien + commentaire]

4. **Une nouvelle vulnérabilité MAVLink** divulguée — qui est
   touché. [Lien + commentaire]

5. **Open Quantum Safe lance sa version 0.12** — ce qui change
   pour les intégrateurs. [Lien + commentaire]

---

📚 **Lecture longue de la quinzaine**
[Recommandation 1 article externe avec courte critique 100 mots]

🎤 **Côté HESIA**
[1-2 nouvelles produit / équipe en 80 mots]

---

À dans deux semaines.
[Fondateur]

P.S. Si cette newsletter vous est utile, faites-la suivre à un
collègue. Inscription publique : hesia.eu/newsletter
```

### 7.3 Croissance liste

- Pop-up exit-intent sur articles de blog (1500 mots+).
- Form en footer du site.
- Mention dans tous les livres blancs ("recevoir nos analyses bi-mensuelles").
- Mention dans les replays webinar.
- Mention LinkedIn fondateur dans la bio + posts épisodiques.

**Cible** : 500 abonnés à T+6 mois, 2000 à T+12 mois.

## 8. Conférences techniques (talks)

### 8.1 Cibles 12 mois

| Conférence | Type | Soumission | Présentation cible |
|-----------|------|------------|--------------------|
| **SSTIC** (Rennes) | Recherche cyber FR | Janvier | "Implémenter ML-KEM dans une TA OP-TEE" |
| **Real World Crypto** (Amsterdam) | Crypto académique | Été N-1 | "Hybrid PQC for embedded systems: lessons" |
| **PQCrypto** (international) | PQC pure | Variable | Selon avancée recherche |
| **Hardware.io** | Hardware sec | Septembre | "Secure boot Jetson — what's broken" |
| **Pass the SALT** (Lille) | FR communauté | Mars | "Hardening drone firmware — patterns" |
| **DEF CON Aerospace Village** | Drone sec | Été | "Anatomy of a drone takeover" |
| **CCC** (Hamburg) | Hacker culture | Octobre | "Sovereign tech in the age of CLOUD Act" |

### 8.2 Stratégie talks

- Soumettre 4-6 talks par an, viser 2-3 acceptés.
- Privilégier les talks **techniques pures** sur les talks "vendeur".
- Toujours sortir un blog post / livre blanc associé pour le SEO long-terme.
- Filmer + ré-uploader sur YouTube HESIA avec accord conférence.
- Mention HESIA discrète (slide intro auteur + slide remerciements).

## 9. Ressources / outils gratuits (lead magnets non-livre-blanc)

### 9.1 Idées de tools / pages utiles

1. **Calculateur d'overhead PQC** — entrée : algo, mode, taille payload → sortie : ko/s overhead, latency added. Page web simple.
2. **Matrice de conformité NIS2/CRA pour fabricants UAS** — interactive, exportable PDF avec coordonnées entrée.
3. **Analyseur de stack crypto drone** — upload SBOM ou liste libs → rapport gaps PQC.
4. **Catalogue des CVEs drones** — base de données indexée recherche.

### 9.2 Avantages

- Drainent du trafic organique fort (intent élevé).
- Crédibilité technique immédiate.
- Lead capture naturel (résultats par email).

### 9.3 Coût et priorité

- T1 : Calculateur PQC (dev simple, 5-10 jours).
- T2 : Matrice NIS2 (form interactif, 7-15 jours).
- T3+ : selon ROI.

## 10. Documentation publique du produit

Bien que le code core HESIA soit propriétaire, certaines docs sont publiques :

- **API reference** (HESIA Command API REST).
- **Specs protocoles** (handshake PQC, formats policy, schéma audit log).
- **Changelog produit** (semver).
- **Roadmap publique 6 mois** (transparence).
- **Status page** (uptime, incidents).

URL : `docs.hesia.eu`. Hébergement statique (Mintlify, Docusaurus, ou GitBook).

## 11. Processus éditorial

### 11.1 Workflow

```
Idée → Brief (1 page) → Plan (markdown) → Brouillon → Relecture
technique → Relecture éditoriale → Visuels → SEO check → Publication
→ Distribution multi-canal → Mesure
```

### 11.2 Outils

- **Idéation** : Notion ou Airtable (board kanban).
- **Rédaction** : éditeur markdown (Obsidian / Cursor / VS Code).
- **Relecture** : Antidote (FR), LanguageTool (EN), GPT en assistant — jamais en générateur principal.
- **Visuels** : Figma + Excalidraw pour schémas techniques.
- **Versionning** : git, sur même repo que le site.
- **CMS** : si site Astro, contenus en .md dans repo. Si Next.js, Sanity ou DatoCMS RGPD-friendly.

### 11.3 Reviewers obligatoires

Tout article passe par 2 paires d'yeux minimum :
1. **Reviewer technique** : un ingénieur HESIA. Vérifie exactitude.
2. **Reviewer éditorial** : fondateur ou marketing lead. Vérifie ton, structure, SEO.

Aucun article n'est publié si l'un des reviewers met un veto.

### 11.4 Cadence interne

- 1 réunion édito mensuelle (2h) : revue calendrier, pipeline 90 jours, ajustements.
- 1 sprint éditorial bi-mensuel (1 semaine intense) : produire 2 articles + visuels + planning distribution.
- Reporting mensuel KPI dans Notion partagé.

## 12. KPI content marketing 12 mois

| KPI | T+3 | T+6 | T+12 |
|-----|-----|-----|------|
| Articles publiés | 6 | 12 | 24 |
| Sessions blog mensuelles | 1 500 | 5 000 | 15 000 |
| Visiteurs uniques /mois | 1 200 | 3 500 | 10 000 |
| Pages vues/session | 1.5 | 1.8 | 2.2 |
| Inscrits newsletter | 200 | 500 | 2 000 |
| Téléchargements livre blanc | 80 | 400 | 1 500 |
| Inscrits webinar | — | 600 | 2 500 |
| Backlinks (referring domains) | 5 | 25 | 80 |
| Mots-clés positionnés top 10 | 15 | 60 | 200 |
| MQL via content | 10 | 50 | 200 |

## 13. Anti-patterns à éviter

- ❌ Publier 5 articles/semaine "pour le SEO" — qualité dilue, autorité dilue.
- ❌ Rédiger les articles intégralement avec un LLM. La cible détecte.
- ❌ Cacher le contenu derrière paywall lourd. Donne une mauvaise image.
- ❌ Faire des articles "SEO bait" sans valeur réelle.
- ❌ Mettre 5 CTAs par article. Distrait, baisse conversion.
- ❌ Republier le même article tous les 3 mois sous prétexte d'actualisation.
- ❌ Stocker les leads sur Google Sheets / Mailchimp → choisir un CRM EU dès le début.

---

**Dernière mise à jour** : 2026-04-25
**Maintenu par** : Marketing & Communication HESIA
