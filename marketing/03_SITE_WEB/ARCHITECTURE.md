# HESIA.eu — Architecture et contenu du site web

**Version** : 1.0 · **Date** : 2026-04-24

---

## 1. Domaine et hébergement

### 1.1 Domaines à réserver
- **hesia.eu** — principal (symbole européen affirmé).
- **hesia.io** — redirection (audience tech).
- **hesia.security** — redirection (audience SEO sécurité).
- Sous-domaines : `www.hesia.eu`, `blog.hesia.eu`, `docs.hesia.eu`, `trust.hesia.eu` (page transparence).

### 1.2 Hébergement
Privilégier un hébergeur européen certifié SecNumCloud ou équivalent :
- **OVHcloud** (FR) — le plus simple.
- **Scaleway** (FR) — alternative.
- **Hetzner** (DE) — moins cher, EU.

Ne **pas** utiliser AWS / Azure / GCP pour le site principal (incohérence marketing avec la souveraineté prônée).

### 1.3 Stack technique recommandée
- **Astro** (générateur site statique) ou **Next.js** static export.
- Markdown-driven pour blog (MDX).
- Déploiement via Git push sur OVH Objects ou Cloudflare Pages EU.
- CDN : Bunny.net (CZ/EU) ou Cloudflare avec zone EU.

### 1.4 Security headers (obligatoires)
```
Strict-Transport-Security: max-age=63072000; includeSubDomains; preload
Content-Security-Policy: default-src 'self'; script-src 'self' 'sha256-...'; style-src 'self' 'unsafe-inline'; img-src 'self' data: https:; font-src 'self' https://fonts.gstatic.com
X-Frame-Options: DENY
X-Content-Type-Options: nosniff
Referrer-Policy: strict-origin-when-cross-origin
Permissions-Policy: geolocation=(), camera=(), microphone=(), fullscreen=(self)
```

### 1.5 Analytics
- **Plausible.io** (self-hosted ou cloud EU) — sans cookies, RGPD natif.
- **Pas de Google Analytics** (incohérent avec la posture souveraineté).

---

## 2. Arborescence du site

```
hesia.eu/
├── /                        # Landing page
├── /product                 # Aperçu produit
│   ├── /core                # HESIA Core (firmware drone)
│   ├── /command             # HESIA Command (serveur)
│   ├── /observe             # HESIA Observe (vidéo)
│   └── /attest              # HESIA Attest (roadmap)
├── /solutions               # Par secteur
│   ├── /critical-infra      # OIV / infrastructure critique
│   ├── /defense             # Défense (teaser, CTA "contact sales")
│   ├── /civil-security      # Sécurité civile, SDIS, douanes
│   └── /integrators         # Intégrateurs drones
├── /security                # Pages transparence
│   ├── /architecture        # Architecture de sécurité
│   ├── /audits              # Audits publiés
│   ├── /disclosure          # Responsible disclosure policy
│   ├── /advisories          # Avis de sécurité (CVE)
│   └── /trust-center        # Trust center (SOC2 roadmap, compliance)
├── /resources
│   ├── /blog                # Articles
│   ├── /whitepapers         # Livres blancs PDF
│   ├── /docs                # Documentation technique (redirect docs.hesia.eu)
│   ├── /webinars            # Replay webinaires
│   └── /case-studies        # Études de cas clients
├── /company
│   ├── /about               # À propos, mission, valeurs
│   ├── /team                # Équipe (profils + photos)
│   ├── /careers             # Recrutement
│   ├── /press               # Press kit + mentions presse
│   └── /contact             # Formulaire contact + adresse postale
├── /legal
│   ├── /privacy             # Politique confidentialité (RGPD)
│   ├── /cookies             # (quasi-vide : on utilise pas de cookies)
│   ├── /terms               # CGV / CGU
│   └── /imprint             # Mentions légales
└── /demo                    # Formulaire demande de démo
```

**Pages prioritaires v1 (MVP)** :
- / (landing)
- /product/core
- /solutions/critical-infra
- /security/architecture
- /resources/blog (avec 3 articles au lancement)
- /company/about
- /demo
- /legal/*

Le reste vient en v2 (2 mois après lancement).

---

## 3. Homepage — structure et copy

### Section 1 : Hero

```
[Logo HESIA en haut à gauche, navigation à droite]

===========================================================

             SOVEREIGN SECURITY,
             POST-QUANTUM READY.

     HESIA secures European industrial and defense drones
     with NIST-standardized post-quantum cryptography
     and hardware-anchored trust.

        [ Request a demo ]   [ Read the whitepaper ]

     ─────────────────────────────────────────────────
     Built for NIS2, CRA and ANSSI compliance.
     Open architecture. Audited. Designed in Europe.
```

Note design : arrière-plan `#0B2545` (Steel Blue), H1 en blanc, accent `#D4A017` (Sovereign Gold) sur "POST-QUANTUM READY". Image de droite : wireframe abstrait d'un drone avec nœuds chiffrés.

### Section 2 : Le problème (pain points)

```
Three realities are reshaping drone security.

[Card 1]
NIS2 is live.
100 000+ European critical operators must now secure their
OT and IoT fleets — including industrial drones.

[Card 2]
Quantum computers are coming.
By 2030, today's RSA and ECC cryptography will start falling.
NIST's August 2024 standards are the new baseline.

[Card 3]
Supply-chain trust is broken.
ITAR, EAR, opaque vendors. European critical infrastructure
needs alternatives — built at home, auditable end-to-end.
```

### Section 3 : Le produit (overview)

```
                ONE SECURE FIRMWARE STACK.
                   FOUR BUILDING BLOCKS.

[Icône hexagone]   HESIA Core
                   Drone firmware hardened with ML-KEM-1024,
                   ML-DSA-87, AES-256-GCM, OP-TEE anchoring.

[Icône serveur]    HESIA Command
                   Ground control server with mTLS 1.3, Ed25519-
                   signed policy, allowlist verification.

[Icône caméra]     HESIA Observe
                   End-to-end encrypted video stream with per-
                   frame AEAD authentication.

[Icône certificat] HESIA Attest
                   Remote attestation with hardware-bound keys.
                   Roadmap Q2 2027.

[ See the architecture → ]
```

### Section 4 : Preuve technique (trust builders)

```
            BUILT ON STANDARDS. AUDITED. DOCUMENTED.

[Logo NIST]      ML-KEM-1024 · ML-DSA-87 · SHA-3/SHA-256
                 Conformant with FIPS 203, 204, 198-1, 180-4

[Logo ANSSI]     On track for CSPN certification.
                 EAL4+ targeted for Q4 2028.

[Logo OpenSSL]   Cryptographic primitives via OpenSSL 3.x EVP
                 and vetted liboqs post-quantum library.

[Logo OP-TEE]    Trusted Application on OP-TEE 4.x, TEE-
                 anchored sealing and attestation.

[ Read our architecture paper → ]
```

### Section 5 : Par secteur (teaser)

```
             WHO TRUSTS HESIA.

[Card] Critical infrastructure
       Utilities, transport, telecom operators aligning with
       NIS2 and preparing for CRA 2027.
       → Learn more

[Card] Defense & security
       European sovereign programs, law-enforcement drone
       fleets, intelligence agencies.
       → Contact sales

[Card] Drone integrators
       OEMs and ODMs embedding HESIA in their PX4, ArduPilot
       or ROS 2-based platforms.
       → Partner with us
```

### Section 6 : Derniers contenus

```
             FROM OUR RESEARCH.

[Thumbnail] [Title: "Post-quantum readiness for industrial drones"]
            [Date: April 2026 · 14 min read]
            [Type: Whitepaper]

[Thumbnail] [Title: "How we bound our drone identity to OP-TEE"]
            [Date: March 2026 · 8 min read]
            [Type: Blog]

[Thumbnail] [Title: "NIS2 compliance for drone operators: a checklist"]
            [Date: Feb 2026 · 10 min read]
            [Type: Blog]

[ See all resources → ]
```

### Section 7 : CTA final

```
                 READY TO SECURE YOUR FLEET?

           Book a 30-minute technical deep dive with
                  our cryptography team.

                  [ Request a demo ]

       Or reach us at hello@hesia.eu · +33 1 XX XX XX XX
```

### Footer

```
HESIA SAS · 14 rue de la République · 75001 Paris
contact@hesia.eu · +33 1 XX XX XX XX · SIREN XXXXXXXXX

  Product    Solutions    Security    Resources    Company
  · Core     · Critical   · Arch.     · Blog       · About
  · Command  · Defense    · Audits    · Papers     · Team
  · Observe  · Civil      · Disclose  · Webinars   · Press
  · Attest   · Integrat.  · CVEs      · Cases      · Careers

  Built in Europe · © 2026 HESIA · Privacy · Terms · Imprint

  [LinkedIn] [X] [YouTube] [GitHub] [RSS]
```

---

## 4. Formulaires

### 4.1 Formulaire "Request a demo" (CTA principal)

Champs :
- Prénom *
- Nom *
- Email professionnel *
- Société *
- Fonction *
- Pays *
- Cas d'usage (dropdown : OIV énergie / transport / télécom / défense / sécurité civile / autre)
- Taille de flotte drones estimée (dropdown : <10 / 10-50 / 50-200 / 200+)
- Urgence (dropdown : exploration / projet < 6 mois / RFQ active)
- Message (textarea, 500 car max)

Anti-bot : Cloudflare Turnstile (privacy-friendly, pas hCaptcha / reCAPTCHA).

Destination :
- Email à sales@hesia.eu avec priority tagging.
- Entrée CRM (HubSpot / Pipedrive / Salesforce).
- Accusé réception automatique avec lien vers livre blanc et calendrier Calendly / SavvyCal.

### 4.2 Formulaire newsletter (inscription livres blancs)
- Email pro *
- Consentement RGPD clair (double opt-in obligatoire).

---

## 5. Pages critiques — copy détaillée

### 5.1 Page `/product/core`

**Titre** : HESIA Core — Post-Quantum Firmware for Autonomous Systems

**Intro (2 paragraphes)** :
> HESIA Core is the software security layer that sits between your drone's operating system and its communication stack. It brings end-to-end post-quantum cryptography, TEE-anchored identity, and policy-signed configuration — without replacing your flight control software.
>
> We integrate with PX4, ArduPilot, ROS 2 and custom Linux stacks on ARM64 (Jetson, i.MX8, Snapdragon, Tegra).

**Subsections** :
1. Cryptographic primitives (ML-KEM-1024, ML-DSA-87, AES-256-GCM, HKDF-SHA2/SHA3, Ed25519)
2. Handshake protocol (HELLO → KEY_INIT → DRONE_AUTH → SERVER_AUTH → CONFIRM)
3. TEE integration (OP-TEE TA, sealing, attestation)
4. TLS 1.3 + exporter binding
5. Policy verification (Ed25519 root)
6. Diagram architecture
7. Spec sheet téléchargeable (PDF)
8. CTA "Request spec sheet" ou "Request source code review"

### 5.2 Page `/security/architecture`

Structure :
- Schéma couches (présentation + transport + TEE + OS).
- Trust model assumptions (ce qu'on trust, ce qu'on ne trust pas).
- Threat model (STRIDE-like ou MITRE ATT&CK mapping).
- Responsible disclosure policy + email `security@hesia.eu`.
- Signature GPG pour vulnérabilités sensibles.

### 5.3 Page `/security/audits`

Liste chronologique :
```
2026-04 — Internal architecture review
2026-09 — External audit by Quarkslab (target)
2027-03 — Cryptographic review by CryptoExperts (target)
2027-06 — Pentest by Synacktiv (target)
```

Chaque audit : résumé public + lien vers rapport complet (accès contrôlé pour clients).

### 5.4 Page `/company/about`

Structure :
- Notre mission (reprise du brand identity).
- Notre histoire (création 2024 ou date réelle, milestones).
- Nos valeurs (3 piliers).
- Pourquoi l'Europe.
- Ce qu'on fait et ce qu'on ne fait pas.
- Affiliations (Hexatrust, Pôle SCS, Aerospace Valley, France Cybersecurity, EU DIGITAL).

---

## 6. SEO — mots-clés cibles

### 6.1 Mots-clés FR prioritaires

**Transactionnels**
- firmware drone sécurisé
- sécurité drone industriel
- cryptographie post-quantique embarquée
- OP-TEE drone
- certification CSPN drone

**Informationnels**
- NIS2 drone
- Cyber Resilience Act drone
- ML-KEM FIPS 203 français
- protection cyber drone industriel
- souveraineté drone France

### 6.2 Mots-clés EN prioritaires

**Transactionnels**
- post-quantum drone firmware
- secure drone firmware Europe
- NIS2 compliant drones
- sovereign drone security
- OP-TEE drone attestation

**Informationnels**
- ML-KEM drone implementation
- CRA drone compliance
- quantum-safe drone
- drone cyber regulation Europe

### 6.3 Stratégie SEO

- **Pillar pages** : une page maître par grand thème (/guides/nis2-drones, /guides/post-quantum-drones).
- **Cluster articles** de blog liés à chaque pillar.
- **Backlinks** cibles : Hexatrust, FIC Media, ANSSI (cas d'usage), GitHub README (via stars et forks), podcasts cyber invités.
- **Publications techniques** sur eprint.iacr.org (renvoient du trafic qualifié).

---

## 7. Versions linguistiques

- **v1** : anglais (lingua franca défense / OIV EU + audience tech internationale).
- **v1.5** : français (2 mois après lancement).
- **v2** : allemand (2027, en même temps qu'expansion DACH).

Pas de traductions automatiques — seulement humaines (Deepl Pro + relecture native).

---

## 8. Performance & accessibilité

### Objectifs Lighthouse
- Performance : > 95
- Accessibility : > 95
- Best Practices : 100
- SEO : > 95

### Contraintes
- Pas de JavaScript bloquant.
- Fonts self-hosted (pas de Google Fonts externe, même si origine EU).
- Images en WebP / AVIF avec fallback PNG.
- Lazy loading sur tout ce qui est sous le fold.
- Prefetch des pages clés (product, demo).
- Score WCAG 2.1 AA minimum.
- Sitemap XML + robots.txt corrects.

### Tests obligatoires avant mise en ligne
- Lighthouse sur 5 pages principales.
- axe DevTools (accessibilité).
- Contraste WebAIM sur toutes les couleurs texte.
- Test lecteur écran (VoiceOver + NVDA).
- Test clavier (navigation complète sans souris).

---

## 9. Checklist mise en ligne

- [ ] Domaine hesia.eu acheté + DNS configuré.
- [ ] Certificat TLS Let's Encrypt (ou équivalent) actif.
- [ ] Redirections www → non-www (ou l'inverse, décision à arbitrer).
- [ ] robots.txt + sitemap.xml.
- [ ] Plausible configuré.
- [ ] Formulaire demo testé end-to-end.
- [ ] Pages légales (RGPD / mentions / CGU) publiées.
- [ ] Responsible disclosure publiée avec clé GPG.
- [ ] Tous les headers sécurité testés sur securityheaders.com (note A+ minimum).
- [ ] Tous les profils sociaux avec lien vers le site.
- [ ] Equipe inscrite sur le RSSI forum / CERT-FR / CERT-EU.
- [ ] Backups quotidiens vérifiés.
- [ ] Monitoring uptime (Uptime Kuma self-hosted ou Better Uptime).

---

## 10. Budget site web v1

| Poste | Coût estimé |
|-------|-------------|
| Design (brand + site) | 10 à 25 k€ |
| Développement front (Astro/Next.js, intégration) | 8 à 20 k€ |
| Copywriting FR/EN | 5 à 10 k€ |
| Hébergement an 1 (OVH/Scaleway) | 300 à 800 € |
| Domaines (hesia.eu + variants) | 100 à 300 €/an |
| Plausible Analytics | 100 €/an |
| Outils form (Formspree ou backend interne) | 0 à 500 €/an |
| **Total v1** | **25 à 60 k€** |

v2 (expansion contenu, traductions) : +15 à 30 k€.
