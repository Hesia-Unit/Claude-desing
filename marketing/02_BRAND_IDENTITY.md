# HESIA — Brand Identity & Style Guide

**Version** : 1.0 · **Date** : 2026-04-24

---

## 1. Le nom

**HESIA**

- 5 lettres, prononçable en français / anglais / allemand / espagnol (hé-SI-a).
- Évoque Hestia, déesse grecque du foyer et du feu sacré — la protection du centre.
- Disponible nom de domaine : vérifier `hesia.eu`, `hesia.io`, `hesia.security`, `hesia.defense`.
- Disponible marque EUIPO classes 9, 42, 45 (vérifier).

### Pourquoi pas un autre nom ?
- "Qryptos", "Postquantum drones", "Kryptos" → overused / confus.
- "Zerberus", "Cerberus" → trop agressif.
- "Aegis" → déjà utilisé (Lockheed, Aegis Systems).
- "HESIA" → court, propre, sans connotation militaire agressive, mémorable.

---

## 2. Mission, vision, valeurs

### 2.1 Mission
> **Permettre à l'Europe de piloter et protéger ses systèmes autonomes critiques avec une cryptographie souveraine et post-quantique.**

### 2.2 Vision
> **À horizon 2030, aucun drone industriel ou de défense en Europe ne devrait voler sans une couche de sécurité auditée, post-quantique et souveraine.**

### 2.3 Valeurs (3 piliers)

**1. Rigueur**
> On écrit du code crypto. On publie nos audits. On n'utilise jamais "probablement sécurisé" ou "basé sur le principe de". Si c'est pas prouvé, on le dit.

**2. Souveraineté**
> Europe first. Open architecture. Pas de backdoor, pas de clé maître, pas d'ITAR, pas d'EAR. Nos clients possèdent leur matériel et leurs clés.

**3. Pragmatisme**
> On ne promet pas la certification défense à 12 mois si elle en prend 24. On construit progressivement, du civil vers le critique, avec honnêteté sur la maturité.

### 2.4 Anti-valeurs (ce qu'on rejette)
- Security theater.
- Marketing hype sans substance technique.
- Fermeture technologique (pas d'OS propriétaire, pas de lock-in).
- Cowboy crypto ("on a inventé notre propre KDF").

---

## 3. Personnalité de marque

### 3.1 Archétype (classification Mark & Pearson)
**The Sage** (Le Sage), avec des traces de **The Hero** (Le Héros).

Le Sage : cherche la vérité, utilise l'expertise, croit au pouvoir de la connaissance. Exemples : IBM, Google, MIT, Harvard.

Le Héros : résout les problèmes par le courage et la compétence. Exemples : Nike, Patagonia côté corporate.

→ HESIA n'est ni Apple (Magician / Creator) ni Red Bull (Explorer / Rebel). Plus proche d'un laboratoire de recherche sérieux + une équipe qui défend un cause.

### 3.2 Brand persona (si HESIA était une personne)
- Genre : non marqué.
- Âge : 35-45 ans.
- Métier : ingénieur cryptographe senior reconverti entrepreneur, avec 10 ans de conseil défense.
- Ton : précis, cultivé, calme, sans esbroufe.
- Références : lit "Cryptography Engineering" (Ferguson), suit Matthew Green sur X, va au SSTIC, parle anglais et allemand.
- Ce qu'il/elle déteste : bullshit marketing, "AI-powered" sans substance, crypto maison non auditée.

### 3.3 Do & Don't de ton

**DO**
- "HESIA implémente ML-KEM-1024 conformément à FIPS 203."
- "Notre canal sécurisé AES-256-GCM a été audité par Quarkslab en 2026."
- "Nous préparons la certification CSPN ANSSI d'ici fin 2027."
- "Voici les limites actuelles de notre TEE sur Jetson SD-boot."

**DON'T**
- "La solution ultime contre les menaces cyber."
- "Blockchain-powered quantum AI drone defense."
- "100 % inviolable."
- "Le leader européen" (tant qu'on ne l'est pas).

---

## 4. Tagline & slogans

### Tagline principale
> **"Sovereign security, post-quantum ready."**

Usage : site web homepage, bas de signature email, 1er slide pitch.

### Taglines secondaires (contextuelles)

Pour audience technique (GitHub, blog, SSTIC) :
> *"Open architecture. Post-quantum cryptography. Hardware-anchored trust."*

Pour audience compliance / RSSI :
> *"NIS2-aligned. CRA-ready. Built for European critical infrastructure."*

Pour audience défense (phase 2) :
> *"Trusted firmware for European autonomy."*

Pour audience FR :
> *"La sécurité souveraine des systèmes autonomes critiques."*

Pour audience investisseurs / institutionnels :
> *"Securing the next generation of autonomous European infrastructure."*

---

## 5. Charte graphique

### 5.1 Logo

Concept proposé : **HESIA** en typographie géométrique sérif + hexagone (référence à "hexa" / structure moléculaire / Europe hexagone).

Description textuelle (à transmettre à designer) :
- Wordmark "HESIA" en capitales, letter-spacing généreux (+40).
- Typographie : **Sora** (Google Fonts, libre) ou **Space Grotesk** (Google Fonts, libre).
- Hexagone vectoriel minimal à gauche ou au-dessus des lettres.
- Version monogramme : "H" dans un hexagone.

Variantes requises :
- Color on white (usage principal).
- White on dark (site hero, présentations).
- Monochrome noir (docs imprimés, fax).
- Monochrome blanc (tampons, broderies).
- Favicon 32×32 : le "H" hexagonal seul.
- App icon 1024×1024 : hexagone + "H" centré.

Formats livrés : SVG (source), PNG 1x/2x/3x, PDF vectoriel, AI/Figma source.

### 5.2 Palette couleurs

Palette minimaliste, 3 couleurs principales + 2 fonctionnelles.

| Rôle | Couleur | HEX | RGB | Usage |
|------|---------|-----|-----|-------|
| Primary | **Steel Blue** | `#0B2545` | 11, 37, 69 | Logo, titres H1, CTA primary, arrière-plan hero |
| Secondary | **Sovereign Gold** | `#D4A017` | 212, 160, 23 | Accents, underline, iconographie critique |
| Neutral dark | **Graphite** | `#1A1E23` | 26, 30, 35 | Textes body, arrière-plan dark mode |
| Neutral light | **Bone** | `#F5F1E8` | 245, 241, 232 | Arrière-plan light mode, papier |
| Success | **Verified Green** | `#2D7A4D` | 45, 122, 77 | États OK, confirmations |
| Danger | **Alert Red** | `#A32020` | 163, 32, 32 | Vulnérabilités, alertes sécurité |

Règles d'usage :
- **Maximum 3 couleurs** sur un même écran.
- Or utilisé **avec parcimonie** : titres clés, iconographie, jamais en aplat large.
- Interdit : dégradés, néon, couleurs consumer (violet / rose / cyan).
- Contraste WCAG AA minimum partout.

### 5.3 Typographie

#### Titres
**Sora** (Google Fonts, SIL OFL 1.1)
- H1 : 56px, weight 600, letter-spacing -1%
- H2 : 40px, weight 600
- H3 : 28px, weight 500
- H4 : 20px, weight 500

#### Corps
**Inter** (Google Fonts, SIL OFL 1.1)
- Body : 17px, weight 400, line-height 1.65
- Body small : 14px
- Lead : 20px, weight 400

#### Code / technique
**JetBrains Mono** (Apache 2.0)
- Code inline : 15px
- Code bloc : 14px, line-height 1.55
- Usage : snippets, config, CLI output

#### Fallback
Stack CSS :
```css
font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
font-family: 'Sora', 'Inter', sans-serif; /* titres */
font-family: 'JetBrains Mono', 'Menlo', 'Consolas', monospace;
```

### 5.4 Iconographie

Principes :
- **Line icons** (pas solid / flat).
- Épaisseur constante 1,5 px.
- Coins arrondis 2 px.
- Librairie recommandée : **Lucide** (ex-Feather Icons, MIT license).
- Pas d'emojis dans les supports officiels.
- Pas d'illustrations humanoïdes cartoon (incompatibles avec ton défense/crypto).

Illustrations photo / 3D :
- Photos abstraites : fibre optique, serveurs, antennes, drones industriels en environnement réel (pas consumer).
- 3D : hexagones, wireframes, réseaux. Palette cohérente.
- Interdit : stock photos "homme en costume devant un globe numérique".

### 5.5 Layout & composition

- **Grille 12 colonnes**, gutter 24px desktop / 16px mobile.
- **Espace blanc généreux** : >40 % de la surface non remplie.
- **Hiérarchie verticale claire** : H1 → body → CTA, pas de tout-en-un.
- **Un CTA principal par section** maximum.
- **Règles des 3 clics** : toute information critique accessible en 3 clics depuis homepage.

---

## 6. Voice & Tone

### 6.1 Voice (voix constante)
- Expert mais pédagogue.
- Calme, factuel, chiffré.
- Pas d'emphase gratuite (pas de "révolutionnaire", "incroyable", "unique").
- Anglais et français maîtrisés au même niveau.

### 6.2 Tone (ton variable selon contexte)

| Contexte | Ton | Exemple |
|----------|-----|---------|
| Homepage | Confiant, direct | *"We secure European drones with post-quantum cryptography."* |
| Blog technique | Précis, enseignant | *"Let's walk through the ML-KEM handshake we use in HESIA Core."* |
| LinkedIn post | Engagé, contextuel | *"NIS2 just hit. Here's what it means for your drone fleet."* |
| Livre blanc | Formel, académique | *"This paper presents the architectural trade-offs between..."* |
| Communiqué presse | Institutionnel | *"HESIA announces the release of its Core v2.0..."* |
| Crisis (incident) | Humble, transparent | *"We identified a vulnerability in v1.3. Here's our timeline."* |

### 6.3 Rules of writing

1. **Pas de "we believe that"** dans un doc technique. On écrit "we implement", "we measure", "we validate".
2. **Pas de superlatif absolu** (meilleur, plus rapide, le seul). Utiliser des chiffres.
3. **Écrire les acronymes en entier à la première occurrence** (PQC = Post-Quantum Cryptography).
4. **Pas de voix passive en français** pour les annonces ("HESIA lance" vs "est lancé par HESIA").
5. **Phrases courtes** : max 25 mots, moyenne 15.

---

## 7. Vocabulaire contrôlé

### 7.1 Termes à privilégier

| ✓ À dire | ✗ À éviter | Pourquoi |
|----------|-------------|----------|
| Firmware drone sécurisé | Firmware drone blindé | "Blindé" est un marketing word |
| Cryptographie post-quantique | Crypto quantique | "Crypto quantique" = computing, pas cryptographie |
| Ancrage matériel | Sécurité hardware | Plus précis |
| Attestation à distance | Preuve à distance | Vocabulaire standard (RATS) |
| Policy signée | Config sécurisée | Vocabulaire standard |
| Opérateurs d'infrastructures critiques | OIV | Sauf audience FR qui connaît l'acronyme |
| Vulnérabilité | Faille | "Vulnérabilité" = vocabulaire responsible disclosure |
| Responsible disclosure | Divulgation responsable | Terme anglais plus utilisé dans la communauté |

### 7.2 Termes interdits
- "AI-powered" (sauf s'il y a vraiment de l'IA et qu'on précise laquelle).
- "Unhackable", "inviolable", "100 % sécurisé".
- "Next-gen" (vide de sens).
- "Blockchain" (sauf si vraiment utilisé).
- "Leader" (sans justification chiffrée).

### 7.3 Mentions obligatoires
- Quand on parle de ML-KEM ou ML-DSA : préciser "FIPS 203 / FIPS 204 standardized by NIST in August 2024".
- Quand on parle d'audit : citer l'auditeur + la date + le périmètre.
- Quand on cite un client : nom + logo nécessitent autorisation écrite.

---

## 8. Sonore / vidéo

### 8.1 Signature sonore
Pas de jingle HESIA pour le moment. Si vidéos produites : signature courte (2 secondes) à la fin avec :
- Son sobre, type "sonar" discret ou note tenue.
- Pas de musique lourde ou épique.

### 8.2 Style vidéo
Vidéos HESIA officielles :
- Format 16:9 principal (site web, YouTube), 9:16 secondaire (LinkedIn stories).
- Voice-over calme, pas hystérique.
- Pas de stock footage "data center avec lumières bleues clignotantes".
- B-roll : code en train d'être écrit, terminaux, oscilloscopes, drones en environnement industriel réel.
- Sous-titres obligatoires (anglais + français).
- Durée : max 90 secondes pour LinkedIn, max 5 minutes pour YouTube.

---

## 9. Usage du logo et éléments de marque

### 9.1 Zone de protection
Distance minimale autour du logo = hauteur de la lettre "H".

### 9.2 Tailles minimales
- Print : 24 mm de large minimum.
- Web : 120 px de large minimum.
- Favicon : 16 px (monogramme seul).

### 9.3 Interdits
- Modifier les couleurs hors charte.
- Ajouter une ombre, un contour, un effet.
- Placer le logo sur un fond image à faible contraste sans calque sombre.
- Utiliser le logo pour valider un produit tiers sans accord.
- Afficher le logo à moins de 10 px d'un autre logo (co-branding : respecter 2× la hauteur).

### 9.4 Co-branding (partenaires)
- Logo HESIA à gauche ou au-dessus, logo partenaire à droite ou en-dessous.
- Taille équivalente.
- Séparateur vertical gris si affichage côte à côte.

---

## 10. Brand assets à produire (livrables designer)

1. **Logo pack** (SVG, PNG, PDF, AI) — 5 variantes.
2. **Brand guidelines PDF** — version consolidée de ce document + visuels (30 pages).
3. **Template PowerPoint / Keynote** — 20 slides types (title, content, quote, chart, closing).
4. **Template document Word / Google Docs** — pour livres blancs.
5. **Template email signature HTML**.
6. **Template LinkedIn banner** (1584×396).
7. **Template post LinkedIn single image** (1200×1200) et carousel (1080×1080).
8. **Template X/Twitter banner** (1500×500).
9. **Template YouTube banner** (2560×1440).
10. **Favicons pack** (16, 32, 180, 192, 512 px).
11. **Business card template** (85×55 mm).
12. **Brand zip package** — tous les assets en un dossier organisé.

Budget designer estimé : 6 à 15 k€ (freelance mid-level) ou 15 à 40 k€ (agence).

Designers spécialisés tech/défense en France à consulter : Studio Amen, Dragon Rouge, Makheia, Jeunes Gens Modernes (+ réseaux Freelance.com / Malt avec filtre "branding B2B tech").
