# 08 — KPI Dashboard & Reporting

> Tableau de bord opérationnel : ce qu'on mesure, à quelle fréquence, et qui est responsable. Toutes les métriques sont **observables** (pas inventées) et reliées à un canal de collecte explicite. Pas de vanity metrics (impressions cumulées, "audience touchée"...).

## 1. Principes de mesure

1. **Observable > spéculatif** : préférer un chiffre vérifiable même petit (50 visites confirmées) à un chiffre extrapolé impressionnant ("portée 800k").
2. **Fréquence adaptée** : weekly pour ops marketing, monthly pour pipeline, quarterly pour stratégie.
3. **Source unique** : chaque KPI a un et un seul outil de référence. Si 2 outils donnent un chiffre différent, on tranche dans la convention.
4. **Honnêteté brutale** : un trimestre raté est admis et analysé. Pas de chiffre "ajusté" pour faire bonne figure en réunion.
5. **Reporting visuel concis** : 1 page A4 pour le summary mensuel. Détails dans annexes.

## 2. Architecture data

```
┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐
│ Plausible (web)  │    │ LinkedIn Insights│    │   CRM Pipedrive  │
└────────┬─────────┘    └────────┬─────────┘    └────────┬─────────┘
         │                       │                       │
         ▼                       ▼                       ▼
┌────────────────────────────────────────────────────────────────┐
│                    Metabase / Notion / Sheet                    │
│              (Tableau de bord unifié — single source)           │
└────────┬───────────────────────────────────────────┬───────────┘
         │                                           │
         ▼                                           ▼
┌──────────────────┐                       ┌──────────────────┐
│ Reporting weekly │                       │ Reporting monthly│
│   (interne)      │                       │   (interne + Co) │
└──────────────────┘                       └──────────────────┘
```

**Outils** :
- **Plausible** : analytics site web. EU-based, RGPD-friendly, pas de cookie.
- **LinkedIn Analytics natif** : page entreprise + ads.
- **CRM Pipedrive ou Brevo** : pipeline commercial, leads, opportunities.
- **Buttondown/ConvertKit Analytics** : newsletter open rate, click rate.
- **Metabase** (open source, self-hosted) : agrégation données.
- **Notion** : reporting narratif mensuel et trimestriel.

## 3. KPI North Star

Une seule métrique en haut du dashboard, qui résume la santé business :

> **ARR signé (annualisé) — pipeline qualifié pondéré**

Formule de pipeline pondéré :
```
ARR pipeline pondéré = Σ (montant opportunity × probabilité stade)
```

Cible année 1 : 800k€ - 1.5M€ ARR signé + 2-3M€ pipeline pondéré.
Cible année 2 : 2.5M€ - 5M€ ARR signé + 5-8M€ pipeline pondéré.

## 4. Niveau 1 — KPI hebdomadaires (ops marketing)

Reporting tous les lundis matin. Format : 1 message Notion + diff vs semaine précédente.

| Catégorie | KPI | Source | Cible 6 mois | Cible 12 mois |
|-----------|-----|--------|--------------|---------------|
| **Site web** | Visiteurs uniques /semaine | Plausible | 800 | 2 500 |
| **Site web** | Pages vues /semaine | Plausible | 1 800 | 6 000 |
| **Site web** | Sessions /visiteur | Plausible | 1.6 | 2.0 |
| **Site web** | Bounce rate | Plausible | < 60% | < 50% |
| **Site web** | Page /demo : visites | Plausible | 25 | 80 |
| **Site web** | Page /demo : conversions | Plausible + CRM | 3 | 12 |
| **LinkedIn page** | Followers | LI Analytics | 800 | 2 000 |
| **LinkedIn page** | Reach posts | LI Analytics | 5 000 | 20 000 |
| **LinkedIn page** | Engagement rate | LI Analytics | 3-4% | 4-5% |
| **LinkedIn perso** | Followers fondateur | LI Analytics | 2 500 | 5 000 |
| **LinkedIn perso** | Vues posts moy. | Shield | 1 500 | 4 000 |
| **GitHub** | Stars repos publics | GH | 80 | 300 |
| **Newsletter** | Abonnés | Buttondown | 500 | 2 000 |
| **Newsletter** | Open rate | Buttondown | > 40% | > 45% |
| **Newsletter** | Click rate | Buttondown | > 8% | > 10% |
| **Webinars** | Replays vues /sem (cumul) | YouTube | 50 | 200 |

## 5. Niveau 2 — KPI mensuels (pipeline + content)

Reporting au 5 de chaque mois pour le mois précédent. Format : Notion narratif + tableau métrique.

### 5.1 Pipeline commercial

| KPI | Source | Cible mois M+6 | Cible M+12 |
|-----|--------|----------------|------------|
| Leads totaux | CRM | 80 | 250 |
| MQL | CRM | 30 | 100 |
| SQL | CRM | 12 | 40 |
| Demos planifiées | CRM | 8 | 25 |
| Demos réalisées | CRM | 6 | 20 |
| Opportunities créées | CRM | 3 | 10 |
| Opportunities closed-won | CRM | 0-1 | 1-3 |
| Pipeline pondéré | CRM | 200k€ | 1M€ |
| Sales cycle moyen | CRM | n/a | 9-12 mois |
| Win rate (closed) | CRM | n/a | 15-25% |
| Ticket moyen closed-won | CRM | n/a | 80k€ |
| MRR/ARR signé | CRM/factu | n/a | 65k€ MRR |

### 5.2 Content marketing

| KPI | Source | M+6 | M+12 |
|-----|--------|-----|------|
| Articles publiés | Blog | 12 | 24 |
| Sessions blog /mois | Plausible | 5 000 | 15 000 |
| Temps moyen sur article | Plausible | > 2 min | > 3 min |
| Téléchargements livre blanc | CRM | 80/mois | 250/mois |
| Inscriptions webinar | Plateforme | 200/sem | 500/sem |
| Show-up rate webinar | Plateforme | 35% | 45% |
| Backlinks (referring domains) | Ahrefs | 25 | 80 |
| Mots-clés positionnés top 10 | Ahrefs | 60 | 200 |
| Mots-clés positionnés top 3 | Ahrefs | 8 | 30 |

### 5.3 Paid media

| KPI | Source | M+6 | M+12 |
|-----|--------|-----|------|
| Budget paid dépensé | Stripe + facturation | 8k€ | 12k€ |
| LinkedIn Ads — impressions | LI Ads | 250k | 600k |
| LinkedIn Ads — clics | LI Ads | 1 800 | 4 000 |
| LinkedIn Ads — CTR | LI Ads | > 0.7% | > 0.9% |
| LinkedIn Ads — CPL | LI Ads | < 250€ | < 200€ |
| Google Ads — clics | GA | 800 | 2 500 |
| Google Ads — conversions | GA | 8 | 30 |
| Google Ads — CPL | GA | < 250€ | < 180€ |
| ROAS paid (sur 12 mois rolling) | calcul | n/a | > 3x |

### 5.4 Brand & RP

| KPI | Source | M+6 | M+12 |
|-----|--------|-----|------|
| Mentions presse (articles) | veille manuelle | 4 | 12 |
| Mentions presse Tier 1 | idem | 1 | 3 |
| Tribunes / bylines publiées | idem | 2 | 5 |
| Talks conférences | calendrier | 1 | 4 |
| Podcasts (en tant qu'invité) | idem | 2 | 8 |

## 6. Niveau 3 — KPI trimestriels (stratégique)

Présentation co-fondateurs + investisseurs si applicable. Format : pitch deck 10 slides + Notion détaillé.

### 6.1 Métriques business

| Métrique | Q1 | Q2 | Q3 | Q4 | Cible an 1 |
|----------|----|----|----|----|-----------|
| ARR signé cumulé | 0 | 80k€ | 250k€ | 800k€ | 800k€-1.5M€ |
| Nombre clients payants | 0 | 1 | 3 | 5-8 | 5-10 |
| MRR | 0 | 7k€ | 20k€ | 65k€ | 65k€+ |
| Cash burn /mois | 50k€ | 70k€ | 90k€ | 100k€ | gérable selon levée |
| Runway en mois | n/a | n/a | n/a | n/a | > 12 mois |
| Headcount | 4 | 5 | 7 | 9-10 | 10 |
| Coût d'acquisition client (CAC) | n/a | n/a | n/a | < 50k€ | calc 12m |
| LTV/CAC ratio | n/a | n/a | n/a | n/a | calc 18m |
| NPS clients | n/a | n/a | > 40 | > 50 | > 50 |

### 6.2 Métriques produit

| Métrique | Q1 | Q2 | Q3 | Q4 |
|----------|----|----|----|----|
| Releases majeures | 1 (Core 1.0) | Core 1.1 | Command 0.5 alpha | Core 1.2 + Attest 0.3 |
| Nouvelles features livrées | tracking | tracking | tracking | tracking |
| Bugs critiques ouverts | < 5 | < 5 | < 3 | < 3 |
| Coverage tests | 60% | 65% | 70% | 75% |
| Time to deploy (CI/CD) | n/a | < 30 min | < 20 min | < 15 min |

### 6.3 Métriques certification / conformité

| Item | Q1 | Q2 | Q3 | Q4 |
|------|----|----|----|----|
| CSPN ANSSI | dossier déposé | instruction | instruction | instruction |
| FIPS 140-3 | analyse | dossier | — | — |
| Common Criteria EAL4+ | — | analyse | analyse | dossier |
| Audit externe stack crypto | — | en cours | rapport | publication partielle |
| Audit pentest externe | — | — | en cours | rapport |
| ISO 27001 (préparation) | — | — | analyse | gap analysis |

## 7. Reporting templates

### 7.1 Weekly ops (tous les lundis 9h)

Format Notion :
```
# Week W##/YYYY — Ops Marketing

## Highlights de la semaine
- [3-5 points clés]

## Métriques web
- Visiteurs uniques : X (vs X la semaine prev, ±X%)
- Conversions /demo : X
- [autres si écart significatif]

## LinkedIn
- Page : X followers (+X), reach top post : X
- Perso : X followers (+X), top post : [titre + reach]

## Pipeline
- Leads : X | MQL : X | SQL : X | Opps : X
- Closes ce mois : X
- Démos prévues : X cette semaine

## Issues à régler
- [problèmes ouverts + owners + due date]

## Décisions à prendre
- [points qui demandent arbitrage]

## Prochaine semaine — focus
- [3-5 priorités]
```

Durée meeting weekly : 30 min max.

### 7.2 Monthly review (1er lundi du mois)

Format Notion + meeting 60 min :
```
# Marketing & Sales Review — [Mois YYYY]

## Executive summary (1 paragraphe)

## Pipeline & Sales
- ARR pipeline pondéré : Xk€ (vs Xk€ prev)
- Closed-won : X deals, Xk€
- Closed-lost : X deals, raisons
- Sales cycle moyen : X mois
- Win rate : X%

## Marketing
- Visiteurs uniques /mois : X
- MQL générés : X (vs cible Y)
- Top 3 articles blog (par sessions)
- Top 3 posts LinkedIn (par engagement)
- Webinars : X inscrits, X présents, X MQL générés

## Paid media
- Budget dépensé : Xk€
- Leads générés : X
- CPL moyen : X€
- ROAS rolling 12m : Xx

## Content
- Articles publiés : X
- Livre blanc téléchargements : X
- Newsletter : X abonnés (+X), open rate X%

## Top decisions
- [décisions clés du mois]

## Risques & blocages
- [points d'attention]

## Plan mois prochain
- [priorités M+1]
```

### 7.3 Quarterly business review (QBR interne)

Format pitch deck 12-15 slides (1h30 réunion) :

```
1. Cover + executive summary
2. North star : ARR signé + pipeline pondéré
3. Pipeline funnel — visualisation conversion stage par stage
4. Marketing performance — channel-by-channel ROI
5. Content performance — top performers + leçons
6. Top deals (won + lost) — analyse approfondie 3-5 deals
7. Customer health — NPS, churn risk, expansion opps
8. Product progress — releases, roadmap, certifications
9. Team & hiring
10. Cash + runway
11. Risques majeurs
12. Plan trimestre suivant — 3 priorités stratégiques
```

## 8. KPI par persona (matrice cross-cutting)

Pour valider que chaque persona avance dans le funnel :

| Persona | Visiteurs site | MQL | SQL | Opps | Closed |
|---------|----------------|-----|-----|------|--------|
| A — Claire (CISO OIV) | track | track | track | track | track |
| B — Marc (Dir. Innov.) | track | track | track | track | track |
| C — Col. Vasseur (Défense) | track | track | track | track | track |
| D — Stéphane (CTO scale-up) | track | track | track | track | track |

Identification persona via :
- Self-declaration (form fields).
- Inférence titre LinkedIn lors de matching.
- Analyse manuelle CRM.

Si une persona ne génère aucun deal après 6 mois → revoir messaging ou retirer du focus.

## 9. KPI canaux (matrice attribution)

Modèle d'attribution **multi-touch** linéaire (chaque touch attribué de manière égale).

| Canal | Visites | Leads | MQL | SQL | Closed | ARR signé |
|-------|---------|-------|-----|-----|--------|-----------|
| Organique blog | track | track | track | track | track | track |
| LinkedIn organique | track | track | track | track | track | track |
| LinkedIn Ads | track | track | track | track | track | track |
| Google Ads | track | track | track | track | track | track |
| Salons | track | track | track | track | track | track |
| Direct (typed URL) | track | track | track | track | track | track |
| Referral (partenaires) | track | track | track | track | track | track |
| Newsletter | track | track | track | track | track | track |
| Webinars | track | track | track | track | track | track |
| Press / RP | track | track | track | track | track | track |

Outils : UTM strict sur tous les liens externes + champ "source" obligatoire en CRM.

## 10. Conventions UTM

Format normalisé pour tout lien externe :

```
?utm_source=[canal]&utm_medium=[type]&utm_campaign=[campagne]&utm_content=[variation]
```

**Sources standardisées** :
- `linkedin` (organic + ads, distinguer par medium)
- `twitter`
- `bluesky`
- `newsletter`
- `webinar`
- `event-fic`, `event-uavshow`, `event-milipol`, etc.
- `press-lemagit`, `press-zdnet`, etc.
- `partner-[nom]`
- `direct-mail`

**Mediums** :
- `cpc` (paid)
- `social` (organique social)
- `email`
- `referral`
- `print`

**Campaigns** : nom court avec date YYYYQQ. Ex : `2026q2-pqc-whitepaper`.

**Contents** : variation créative. Ex : `headline-a`, `image-blue`.

Exemple complet :
```
https://hesia.eu/resources/pqc-migration?utm_source=linkedin&utm_medium=cpc&utm_campaign=2026q2-pqc-whitepaper&utm_content=ciso-headline-a
```

## 11. Outils — coût total annuel estimé

| Outil | Usage | Coût annuel |
|-------|-------|-------------|
| Plausible | Analytics web | 110€ |
| Pipedrive (5 sièges) | CRM | 1 800€ |
| Buttondown | Newsletter | 290€ |
| Metabase | Dashboard self-hosted | 0€ (hosting compris) |
| LinkedIn Sales Navigator (2 licences) | Prospection | 1 800€ |
| Apollo.io | Outreach | 1 200€ |
| Dropcontact | Enrichissement EU | 600€ |
| Cal.com | Calendar | 240€ |
| Yousign | Signature électronique | 540€ |
| Modjo | Recording sales calls | 1 800€ |
| Ahrefs (1 siège) | SEO | 2 400€ |
| Canva Pro | Visuels rapides | 130€ |
| Figma Pro | Design | 180€ |
| Notion | Documentation | 240€ |
| **TOTAL stack data/sales/marketing** | | **~11 000€/an** |

Détails budget complet : `09_BUDGET_ROADMAP.md`.

## 12. Gouvernance données

### 12.1 RGPD strict

- Tous les outils utilisés : analyse RGPD documentée (DPA signé, hébergement EU privilégié).
- Registre de traitement à jour.
- Politique de rétention : 24 mois max sur les leads non convertis. Suppression automatique programmée.
- Droits utilisateurs (accès, rectification, suppression) traitables sous 30 jours via process documenté.

### 12.2 Souveraineté data

- Aucune donnée client ou prospect sur outils US sans DPA + clauses contractuelles type.
- Pas d'export CRM vers Google Sheets / Drive.
- Sauvegardes chiffrées sur infrastructure EU.

### 12.3 Sécurité

- Authentification forte (TOTP) sur tous les outils.
- Politique de mots de passe via gestionnaire (1Password, Bitwarden).
- Accès par rôles (least privilege).
- Audit trimestriel des accès.

## 13. Maturité dashboard — phasage

### 13.1 Phase 1 (M+0 à M+3)

- Plausible installé.
- LinkedIn Analytics natif.
- CRM avec scoring manuel.
- Reporting hebdomadaire Notion.
- Pas encore Metabase.

### 13.2 Phase 2 (M+3 à M+6)

- Metabase déployé self-hosted.
- Connecteurs CRM + Plausible + Newsletter.
- Premier dashboard unifié.
- Reporting mensuel formalisé.

### 13.3 Phase 3 (M+6 à M+12)

- Attribution multi-touch automatisée.
- KPI personas mesurés.
- Forecasting pipeline (modèle simple Excel ou Notion).
- Reporting trimestriel structuré.

### 13.4 Phase 4 (M+12+)

- Prévisionnel financier ARR + cash flow.
- Cohort analysis clients.
- A/B testing framework site et emails.
- Outils plus matures si nécessaire (Mixpanel EU, Amplitude EU).

## 14. Anti-patterns à éviter

- ❌ Mesurer "impressions cumulées" comme KPI principal.
- ❌ Suivre 50 KPI sans hiérarchie.
- ❌ Ajuster les chiffres pour les rendre plus "présentables".
- ❌ Avoir 3 sources de vérité différentes (CRM, sheet, dashboard).
- ❌ Pas de conventions UTM → attribution impossible.
- ❌ Pas mesurer le coût de chaque canal (donc pas d'arbitrage possible).
- ❌ Reporting mensuel sans actions concrètes en sortie.
- ❌ Pas de seuil de déclenchement (ex : "si CPL > 350€ pendant 4 semaines, on coupe").

## 15. Seuils de déclenchement (alarmes)

| Métrique | Seuil rouge | Action |
|----------|-------------|--------|
| Visiteurs site /sem | < 500 pendant 3 sem | Investigation SEO + content review |
| LinkedIn Ads CPL | > 400€ pendant 4 sem | Pause + relance audience/créa |
| Newsletter open rate | < 30% pendant 2 mois | Refonte ligne éditoriale |
| Webinar show-up | < 25% sur 3 webinars | Revoir promotion + UX inscription |
| Pipeline pondéré | < 50% objectif trim | Plan d'urgence sales + marketing |
| NPS | < 30 | QBR clients + plan correctif |
| Cash runway | < 9 mois | Plan de levée ou réduction de coûts |

---

**Dernière mise à jour** : 2026-04-25
**Maintenu par** : Marketing & Operations HESIA
