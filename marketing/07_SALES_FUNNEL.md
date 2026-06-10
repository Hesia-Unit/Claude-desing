# 07 — Pipeline Commercial & Sales Funnel

> Modèle commercial B2B enterprise : cycles longs (6-18 mois), tickets élevés (15k€ - 500k€), peu de comptes mais à fort enjeu. La méthode est qualifiée MEDDIC, le pipeline est tracé dans un CRM EU, le scoring est explicite. Pas de SDR call mass-market, pas de cold email automatisé volume.

## 1. Modèle commercial cible

### 1.1 Typologie deals

| Type | Ticket | Cycle | Volume annuel cible (T+12) |
|------|--------|-------|----------------------------|
| **POC / Pilot** | 20-60k€ | 3-6 mois | 6-10 |
| **Licence Core** | 50-200k€ | 6-12 mois | 4-8 |
| **Licence Command + intégration** | 150-500k€ | 9-18 mois | 1-3 |
| **Audit / Assessment** | 15-30k€ flat | 1-3 mois | 8-12 |
| **Support / Maintenance** | 20% MRR | renouvellement annuel | n/a |

### 1.2 Buyer personas (résumé — détails dans `01_PLAN_MARKETING_STRATEGIQUE.md`)

- **Persona A — Claire**, CISO grand opérateur OIV. Décideuse économique. Critères : conformité NIS2, souveraineté, audit.
- **Persona B — Marc**, Directeur Innovation industriel. Champion technique. Critères : intégration, performance, ROI.
- **Persona C — Col. Vasseur**, Officier programme défense. Décideur militaire. Critères : sécurité, certification, souveraineté ITAR-free.
- **Persona D — Stéphane**, CTO scale-up drone. Acheteur OEM. Critères : prix, time-to-integration, maintenance.

## 2. Pipeline — étapes et critères

### 2.1 Funnel HESIA (7 étapes)

```
[1] AWARENESS         — visiteur site, lecteur blog, abonné LinkedIn
        ↓
[2] LEAD              — formulaire rempli (whitepaper, newsletter, demo)
        ↓
[3] MQL (Marketing Qualified Lead)
        ↓
[4] SQL (Sales Qualified Lead)  — RDV qualification effectué
        ↓
[5] OPPORTUNITY       — POC / proposal envoyé
        ↓
[6] NEGOTIATION       — terms en discussion
        ↓
[7] CLOSED-WON / LOST — signé ou perdu
```

### 2.2 Critères de transition entre étapes

| De → Vers | Critère(s) |
|-----------|------------|
| Lead → MQL | Entreprise dans liste cible OU titre dans liste cible OU score lead ≥ 30 |
| MQL → SQL | RDV qualif effectué + 4/6 critères MEDDIC validés |
| SQL → Opportunity | Demande POC / proposal explicite + budget identifié |
| Opportunity → Negotiation | POC réussi (KPI atteints) OU termes contractuels discutés |
| Negotiation → Closed-Won | Bon de commande / contrat signé |

### 2.3 SLA internes

| Action | SLA |
|--------|-----|
| Lead → premier contact | 24h ouvrées |
| Demande demo → RDV proposé | 48h |
| RDV qualif → compte-rendu CRM | 24h après RDV |
| Proposal envoyée → suivi | 7 jours |
| Pas de réponse 2 semaines | Relance + escalation |

## 3. MEDDIC — qualification d'opportunité

### 3.1 Les 6 critères

**M — Metrics** : quel résultat mesurable apporte HESIA au prospect ?
- Ex : "Réduire de 80% le temps d'audit conformité NIS2 par drone."
- Ex : "Éliminer 12 vulnérabilités critiques identifiées dans l'audit interne."

**E — Economic Buyer** : qui signe le bon de commande ?
- Pas le RSSI s'il faut une signature DSI / DG.
- Identifier nominativement.

**D — Decision Criteria** : quels critères techniques et business pour choisir un fournisseur ?
- Conformité (NIS2, CRA, ANSSI homologation requise ?).
- Souveraineté (origine, hébergement, contractuel).
- Performance technique.
- Prix.
- Capacité à passer un audit interne.

**D — Decision Process** : qui valide quoi, dans quel ordre ?
- Évaluation technique (équipe sécurité) → 4-8 semaines.
- Validation conformité (legal/compliance) → 4-6 semaines.
- Comité achat / DG → 1-3 mois.
- Total : 4-12 mois pour POC, 12-24 mois pour licence + déploiement.

**I — Identify Pain** : quel problème spécifique HESIA résout pour CE prospect ?
- Audit interne récent qui a révélé des gaps.
- Échéance NIS2 / CRA proche.
- Incident cyber évité de justesse.
- Migration matérielle planifiée.

**C — Champion** : qui en interne pousse pour HESIA ?
- Idéal : RSSI ou architecte sécurité avec accès à l'Economic Buyer.
- Sans champion → opportunité fragile.

### 3.2 Template fiche opportunité

```
Compte : [Nom entreprise]
Secteur : [Énergie / Transport / Défense / etc.]
Effectif : [taille]
Site : [url]
Date entrée pipeline : [date]
Stade actuel : [Lead / MQL / SQL / Opportunity / Negotiation]
Probabilité : [10% / 25% / 50% / 75% / 90%]
Montant prévu : [€]
Date close prévue : [date]

──────────────────────────
M — Metrics
[résultat mesurable attendu par le prospect]

E — Economic Buyer
[Nom — Titre — niveau de proximité]

D — Decision Criteria
1. ...
2. ...
3. ...

D — Decision Process
[étapes + délais estimés]

I — Identify Pain
[douleur primaire validée]

C — Champion
[Nom — Titre — engagement level]
──────────────────────────

Historique
[YYYY-MM-DD] Action — résumé
[YYYY-MM-DD] Action — résumé

Prochaines étapes
- [ ] action 1 (resp. + date)
- [ ] action 2 (resp. + date)

Risques
- ...

Concurrents identifiés
- ...
```

## 4. Lead scoring

### 4.1 Modèle de scoring (0-100)

**Démographique (max 50 pts)** :
| Attribut | Points |
|----------|--------|
| Entreprise dans liste top-30 ABM | +25 |
| Entreprise OIV (LPM ou ANSSI list) | +15 |
| Secteur primaire (énergie, transport, défense, télécom) | +10 |
| Secteur secondaire (santé, finance, public) | +5 |
| Géographie EU/EEE | +10 |
| Géographie hors EU non-cible | -10 |
| Taille entreprise > 1000 employés | +5 |
| Taille < 50 employés (sauf scale-up drone) | -10 |
| Titre prioritaire (CISO, RSSI, CTO, dir. innov.) | +15 |
| Titre intermédiaire (architecte, ingé sécu) | +8 |
| Titre junior / non-technique | +2 |

**Comportemental (max 50 pts)** :
| Action | Points |
|--------|--------|
| Téléchargé un livre blanc | +15 |
| Inscrit à un webinar | +10 |
| Présent au webinar live | +15 |
| Demandé une demo | +30 |
| Visité 5+ pages site (90j) | +10 |
| Visité page pricing | +20 |
| Visité page demo | +25 |
| Ouverture 3+ emails newsletter | +10 |
| A interagi avec posts LinkedIn | +5 |
| A répondu à un cold email | +20 |

**Seuils d'action** :
- 0-29 : Lead. Pas d'action sales. Nurture par newsletter et content.
- 30-49 : MQL. Email personnalisé ou InMail dans la semaine.
- 50-69 : SQL. Tentative RDV qualif sous 48h.
- 70+ : Hot. Appel direct si possible, escalation fondateur si compte cible.

### 4.2 Outil

Score automatique calculé dans le CRM (HubSpot ou alternative EU comme Brevo, Pipedrive).

## 5. CRM & stack outils commerciaux

### 5.1 Choix CRM

**Critères** :
- Hébergement EU (RGPD, NIS2 transparence).
- API ouverte.
- Pas de lock-in fort.
- Coût raisonnable < 200€/mois pour 5 sièges.

**Candidats** :
| Outil | Hébergement | Prix indicatif |
|-------|-------------|----------------|
| **Pipedrive** | EU possible | 30-60€/siège/mois |
| **Brevo (ex-Sendinblue)** | FR | 20-50€/siège/mois |
| **HubSpot** | mixte (EU sur Enterprise) | 50-150€/siège/mois |
| **NocoDB / Baserow + Plausible + Buttondown** | self-hosted EU | 30€/mois total |

**Recommandation initiale** : Pipedrive ou Brevo pendant Phase 1. HubSpot Enterprise quand l'équipe sales atteint 5+ personnes.

### 5.2 Stack complète

| Fonction | Outil | Notes |
|----------|-------|-------|
| CRM | Pipedrive / Brevo | EU-friendly |
| Email outreach | Apollo.io ou Lemlist | volume mesuré |
| Lead enrichment | Dropcontact (FR) | RGPD-OK |
| Analytics web | Plausible | EU, no cookie |
| Calendar | Cal.com (open source) | self-hosted possible |
| eSignature | Yousign (FR) | RGPD-friendly |
| Demo booking | Cal.com | |
| Visio | Whereby ou Jitsi | EU |
| Knowledge base interne | Notion | mixte mais accepté |
| Document management | OnlyOffice / Cryptpad | EU |
| Proposal | PandaDoc ou template Pages/Docs | export PDF |
| Recording / transcription | Modjo (FR) | RGPD |

### 5.3 À éviter

- ❌ Salesforce — coût + complexité + non-EU non-justifiable au stade actuel.
- ❌ Outreach.io — non-EU + risque CLOUD Act sur les données prospects.
- ❌ ChatGPT pour rédiger des emails sales personnalisés sans review.
- ❌ Calendly avec free plan (data US).

## 6. Outbound stratégique

### 6.1 Approche

L'outbound HESIA est **chirurgical**, pas volume. Cible : 30 comptes ABM + 50 comptes secondaires identifiés. Total prospects nominatifs : ~250.

Pas de cold email scrappé en masse. Pas d'automation aveugle.

### 6.2 Workflow par compte cible

```
1. Recherche compte (1-2h)
   → Actu récente, organigramme, signaux d'achat
2. Identification 2-4 personas dans le compte
3. Personnalisation triple :
   → Compte (actu)
   → Persona (problème métier)
   → Produit (solution alignée)
4. Premier touch via :
   → LinkedIn engagement (commentaire pertinent)
   → InMail (4-7 jours après engagement)
   → Email pro (8-10 jours après InMail si pas réponse)
5. Cadence relance :
   → +3 jours (nouvelle valeur)
   → +7 jours (case study connexe)
   → +14 jours (referral demande)
   → STOP (jamais > 4 touches)
6. Si pas de réponse : drop dans nurture content (pas relance directe)
   pendant 6 mois minimum.
```

### 6.3 Exemples sequences

**Sequence A — CISO grand compte énergie**

**Touch 1 — LinkedIn comment** (jour 0)
Sur un post du CISO ou d'un ingé sécu de l'entreprise. Commentaire substantiel (3-4 lignes), question pertinente. Pas de pitch.

**Touch 2 — LinkedIn InMail** (jour 5)
```
Bonjour [Prénom],

J'ai noté votre commentaire sur [post / sujet]. Chez [Entreprise], votre
flotte de drones d'inspection [secteur précis si public] doit
représenter un sujet montant en visibilité — surtout avec NIS2 qui
formalise les obligations sur les composants embarqués critiques.

Je dirige HESIA, on conçoit le firmware de sécurité post-quantique pour
flottes UAS. Notre matrice de conformité NIS2 + UAS pourrait être utile
à vos équipes — 18 pages, pas de gating commercial :

[Lien direct livre blanc]

Si l'angle vous intéresse, je peux organiser 30 minutes d'échange sans
pitch — juste pour comparer architectures avec votre équipe.

[Lien Calendly]

Bien à vous,
[Fondateur]
```

**Touch 3 — Email pro** (jour 12, si pas de réponse InMail)
```
Objet : Conformité NIS2 + drones d'inspection [Entreprise]

Bonjour [Prénom],

Je vous écris suite à mon InMail LinkedIn de la semaine dernière (resté
sans réponse — je comprends, vos messages doivent saturer).

Un point concret : vos confrères de [secteur] (que je ne nommerai pas
ici) traitent actuellement la migration des firmwares drones vers les
algorithmes NIST FIPS 203/204. Le calendrier ANSSI pour l'horizon 2030
est tendu si on prend en compte les cycles de qualification.

J'ai pensé à vous parce que [Entreprise] a la chance d'avoir une équipe
sécurité reconnue (j'ai lu plusieurs publications de [collègue cité]).

Notre matrice de conformité NIS2 + UAS répond aux 12 questions les
plus fréquentes que nous recevons :
[Lien direct]

Si vous voulez 30 minutes pour comparer notes, mon agenda :
[Lien Calendly]

Sinon, simplement répondre "pas pertinent" suffit, je n'insisterai pas.

Cordialement,
[Fondateur]
HESIA · hesia.eu
```

**Touch 4 — Email valeur** (jour 19)
```
Objet : 1 question rapide [Prénom]

Bonjour [Prénom],

Je ne vous relancerai plus après ce message — je préfère rester utile
sans devenir intrusif.

Une seule question si vous avez 10 secondes : la migration cryptographique
de vos drones, c'est :

A) Sujet déjà traité en interne / partenaire en place
B) Sujet identifié, pas encore staffé
C) Pas dans les priorités 2026
D) Hors-périmètre HESIA

Cela m'aide à comprendre où vous en êtes — et à ne pas vous solliciter
si ce n'est pas pertinent.

Merci par avance,
[Fondateur]
```

**Touch 5 — Drop nurture**
Si pas de réponse, le prospect retourne dans la base content nurture (newsletter + LinkedIn) pendant 6 mois minimum. Aucune relance directe.

### 6.4 Règles éthiques outbound

- Email personnalisé OBLIGATOIRE par compte. Pas de mail merge brutal.
- Opt-out clair dans tous les emails ("répondez 'stop' et je vous retire").
- Pas plus de 4 touches actives.
- Pas de scraping LinkedIn agressif (PhantomBuster aggressive ou similaire).
- Respect strict RGPD : base légale "intérêt légitime B2B" documentée.

## 7. Inbound demo / contact

### 7.1 Process demo request

```
1. Form submit /demo (ou form fin de livre blanc avec coche "demo")
2. Lead reçu dans CRM avec score initial
3. Auto-email de confirmation (J0, automatique)
   → "Merci [Prénom], on revient vers vous sous 24h"
4. Sales lead checke le score + recherche compte (1h)
5. Si score >= 50 : InMail/email perso avec 3 créneaux Calendly (J0-J1)
6. Si score < 50 : email perso avec 2 créneaux Calendly + lien
   livre blanc complémentaire
7. Demo planifiée (30-45 min)
8. Compte-rendu CRM J+0
9. Email follow-up J+1 avec :
   → Récap de la démo
   → Ressources mentionnées
   → Prochaines étapes proposées
10. Si chaud → opportunity créée
    Si tiède → drop dans nurture
```

### 7.2 Format demo type (45 min)

**0-5 min** : Découverte rapide. "Avant de plonger, dites-moi 2 minutes : votre flotte, vos drones, votre principal sujet de sécurité aujourd'hui."

**5-15 min** : Présentation HESIA en 5-6 slides max.
- Le problème (positionné autour de leur enjeu déclaré).
- L'architecture HESIA en 1 slide.
- 2-3 fonctionnalités clés en démo live.
- Roadmap honnête + ce qui n'est pas fait.

**15-35 min** : Démo live.
- Boot d'un drone simulé (ou Jetson Orin physique).
- Affichage attestation OP-TEE (mock ou réel selon stade).
- Handshake PQC visible dans logs.
- Signature et déploiement d'une policy.
- Audit log dans le SIEM cible.

**35-45 min** : Q&A + next steps.
- Questions techniques.
- Discussion conformité.
- Proposition de POC ou audit court.
- Next meeting planifié.

### 7.3 À ne PAS faire en demo

- ❌ Slides corporate sur 20 minutes sans démo live.
- ❌ Promettre une fonctionnalité non développée.
- ❌ Cacher les limitations actuelles (TRL, certifications en cours).
- ❌ Comparer agressivement à un concurrent nommé.
- ❌ Dépasser 45 min sans accord explicite.

## 8. Proposals & contrats

### 8.1 Template proposal POC (10-15 pages)

```
1. Couverture (1p)
2. Executive summary (1p)
3. Compréhension du contexte client (1-2p)
4. Objectifs du POC (1p, SMART)
5. Scope technique (2-3p)
   → composants HESIA installés
   → infrastructure cible
   → données / logs collectés
6. Critères de succès (1p)
   → KPI mesurables
   → seuils acceptation
7. Calendrier (1p, Gantt simple)
8. Équipe et responsabilités (1p)
   → côté HESIA
   → côté client
9. Conditions financières (1p)
   → forfait POC
   → modalités paiement
   → conditions évolution vers contrat licence
10. Conditions juridiques résumées (1p)
11. Annexes : références techniques, schémas (variable)
```

### 8.2 Formats contractuels

- **POC** : forfait fixe + clause go/no-go. Délai max 6 mois.
- **Licence Core** : annuelle, renouvelable. Tarif par drone par an. Volumes 10/50/250/1000+.
- **Licence Command** : annuelle, par instance serveur + sièges utilisateurs.
- **Audit / Assessment** : forfait fixe, livrable rapport sous 4 semaines.
- **Support** : 20% MRR licence. SLA tiers Bronze/Silver/Gold.

### 8.3 Pricing (rappel — détails dans `01_PLAN_MARKETING_STRATEGIQUE.md`)

| Offre | Tarif catalog | Réduction max |
|-------|---------------|----------------|
| HESIA Core | 2 500€/drone/an | -30% volume 250+ |
| HESIA Command | 15 000€/an + sièges | -20% volume |
| HESIA Observe | 1 500€/drone/an | -25% volume |
| HESIA Attest | 800€/drone/an | -25% volume |
| Audit / Assessment | 20 000€ flat | rare |
| Support Gold | 20% MRR | non négociable |

### 8.4 Négociation

- **Discount limite** : -30% sur tarif catalog (au-delà → escalation fondateur).
- **Conditions de paiement** : 50/50 (signature/livraison) standard. 30/30/30/10 sur projets > 200k€.
- **Pénalités** : 1% par semaine de retard côté HESIA. Cap 10% du montant total.
- **Garanties** : performance KPI POC documentée, sinon avoir 50% sur licence Y1.

## 9. Onboarding client

### 9.1 Phases (durée totale 8-16 semaines selon scope)

```
Phase 1 — Kick-off (semaine 1)
- Réunion de lancement (côté HESIA + client + intégrateur si applicable)
- Validation scope, calendrier, KPI
- Désignation référents techniques et opérationnels
- Plan de communication (réunion hebdo + reporting bi-mensuel)

Phase 2 — Setup infra (semaines 2-4)
- Provisioning environnement test client
- Installation HESIA Core sur drones pilotes
- Configuration HESIA Command côté serveur client
- Connexion SIEM si applicable

Phase 3 — Tests intégration (semaines 4-8)
- Validation handshake PQC end-to-end
- Tests attestation et boot signé
- Tests sandbox vidéo / IA
- Validation logs audit transmis correctement

Phase 4 — Validation conformité (semaines 6-12)
- Documentation matrice NIS2 / CRA générée
- Préparation audit interne ou externe
- Formation équipes ops client

Phase 5 — Bascule production (semaines 12-16)
- Déploiement progressif sur flotte production
- Monitoring + ajustements
- Formation finale
- Réunion clôture + bilan + KPI atteints
```

### 9.2 Responsabilités

| Côté HESIA | Côté client |
|-----------|-------------|
| Customer Success Manager | Sponsor exécutif |
| Senior Security Engineer | Référent sécurité |
| Solution Architect | Architecte SI |
| Documentation produit + custom | Accès infra + drones |
| Formation 2j sur site | Disponibilité équipes ops |

### 9.3 Livrables

- Documentation de déploiement personnalisée.
- Matrice de conformité NIS2 / CRA spécifique.
- Rapport d'audit pré-bascule.
- Procédures opérationnelles standardisées (POS).
- Plan de réponse à incident.
- Compte-rendu de bilan + lessons learned.

## 10. Customer success & expansion

### 10.1 QBR (Quarterly Business Reviews)

Tous les clients > 50k€/an ont droit à 4 QBRs par an :
- Bilan KPI sécurité (incidents, MTTR, conformité).
- Roadmap produit HESIA pertinente pour eux.
- Identification expansion (nouveaux drones, nouveaux modules).
- Renégociation conditions si nécessaire.

### 10.2 NPS

Mesure trimestrielle. Cible : > 40 année 1, > 50 année 2.

### 10.3 Expansion vectors

- **Volume** : ajouter des drones à la licence existante.
- **Module** : ajouter HESIA Observe ou Attest à un client Core.
- **Service** : audit annuel récurrent.
- **Filiales** : adresser d'autres BU du même groupe.

### 10.4 Renouvellement

- 90 jours avant échéance : entretien renouvellement.
- 60 jours avant : proposition de renouvellement.
- 30 jours avant : signature.
- Négociation tarif au renouvellement uniquement si volume change.

## 11. Métriques sales

### 11.1 KPI mensuels

| KPI | Cible année 1 | Cible année 2 |
|-----|---------------|---------------|
| MQL générés | 30/mois (T+12) | 80/mois |
| SQL générés | 12/mois (T+12) | 30/mois |
| Demos planifiées | 8/mois (T+12) | 20/mois |
| Opportunities créées | 3/mois (T+12) | 8/mois |
| Closed-won | 1/trimestre (T+12) | 1/mois |
| Sales cycle moyen | 9-12 mois | 6-9 mois |
| Ticket moyen | 80k€ | 150k€ |
| Win rate | 15-20% | 25-30% |
| ARR | 800k€ - 1.5M€ | 2.5M€ - 5M€ |
| Churn | n/a (pas encore) | < 10% |

### 11.2 Reporting

- Pipeline review hebdomadaire (45 min, équipe sales).
- Forecast mensuel (commit / best case / pipeline).
- QBR interne trimestriel (équipe + fondateurs).
- Annual review.

### 11.3 Outils reporting

- Pipeline visualization : natif CRM.
- Dashboards : Notion ou Metabase.
- Source de vérité unique : CRM. Pas de Google Sheet parallèle.

## 12. Recrutement équipe sales

### 12.1 Phasage

| T | Profils |
|---|---------|
| Mois 0-6 | Fondateur fait 100% du sales. Pas de recrutement. |
| Mois 6-12 | 1 BDR / Account Executive senior (8-12 ans XP B2B enterprise). |
| Mois 12-18 | + 1 Customer Success Manager. |
| Mois 18-24 | + 1 Account Executive junior (3-5 ans XP) ou 1 SE (Sales Engineer). |
| Mois 24+ | Construire équipe géo (DACH, BeNeLux, sud EU). |

### 12.2 Profil AE senior

- Expérience B2B SaaS / cyber / défense > 8 ans.
- Cycle long (> 6 mois) maîtrisé.
- Anglais courant.
- Réseau dans secteurs cibles (énergie, défense, transport).
- Salaire indicatif : 70-100k€ fix + variable 50-80k€.

### 12.3 Pas de SDR junior à froid

Modèle traditionnel SDR-AE inadapté à HESIA :
- Cycle trop long pour des juniors.
- Cible trop senior pour appel à froid d'un junior.
- Préférer 1 AE expérimenté qui owne le full cycle pendant 18 mois.

## 13. Anti-patterns à éviter

- ❌ Pousser la signature avant que le besoin client soit qualifié.
- ❌ Discounter sans contrepartie (volume, durée, étude de cas).
- ❌ Promettre une roadmap fonctionnalité pour signer un deal.
- ❌ Forcer un POC sur un compte qui n'est pas champion-aligned.
- ❌ Sourcer des leads sur des bases de données scrappées.
- ❌ Sales commission > 25% du salaire fixe (encourage la sur-promesse).
- ❌ Vendre uniquement aux RSSI sans embarquer les opérationnels.
- ❌ Refuser un POC payant car "le client va se faire la main" (au contraire — payant = engagement).

---

**Dernière mise à jour** : 2026-04-25
**Maintenu par** : Sales & Customer Success HESIA
