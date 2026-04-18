# AGENT.md — SPECTRA (Rafale) : compréhension technique (sources publiques)

> **But** : donner à un agent (ou à un nouveau contributeur) une compréhension structurée de ce qu’est **SPECTRA** sur le **Rafale**, de ses fonctions, de ses interfaces, et des concepts physiques/informatiques associés — **sans détails classifiés** et **sans procédures d’attaque**.  
> **Périmètre** : informations **publiques** + principes génériques de guerre électronique (EW).

---

## 1) Définition (ce que c’est)

**SPECTRA** signifie **Système de Protection et d’Évitement des Conduites de Tir du Rafale**.  
C’est la **suite de guerre électronique intégrée** du Rafale : un ensemble *capteurs RF/IR/laser + traitement temps réel + décideur + contre-mesures*, conçu pour **détecter**, **identifier**, **localiser** et **réduire l’efficacité** des menaces (radars, missiles, désignateurs), tout en fournissant au pilote une **situation tactique** fusionnée.

Idée clé : ce n’est pas “un brouilleur”. C’est une **architecture complète de survivabilité** combinant :
- **ESM** (Electronic Support Measures) : écoute/détection/identification passive en RF
- **ECM** (Electronic Counter-Measures) : contre-mesures RF (brouillage, tromperie) et gestion de leurres
- **Alerte missile** (MAWS) : détection d’un tir/approche missile
- **Alerte laser** (LWR) : détection d’illumination/désignation laser
- **Leurres** : chaff/flares et éventuellement autres moyens
- **Fusion/commande** : traitement central et intégration avec avionique (radar, optronique, navigation, liaisons de données)

---

## 2) Objectifs opérationnels (ce que ça cherche à obtenir)

### 2.1 Survivabilité
SPECTRA vise à **réduire la probabilité** que le Rafale :
- soit détecté / classifié,
- soit accroché en poursuite,
- soit engagé avec succès (missile / artillerie / conduite de tir).

### 2.2 Aide à la décision et autonomie
Le système fournit :
- une **image des menaces** (type, direction, priorité),
- des **recommandations** (manœuvre, trajectoire, altitude),
- des **réponses automatiques** dans certains cas (séquences de leurres, gestion du brouillage).

### 2.3 Capteur passif stratégique
Grâce à la **géolocalisation passive** (estimations d’angle/temps), SPECTRA contribue à :
- cartographier des émetteurs ennemis,
- enrichir la situation tactique,
- éventuellement partager via datalink (selon configuration/mission).

---

## 3) Architecture (vue d’ensemble)

### 3.1 Architecture distribuée
Les **antennes/capteurs** ne sont pas concentrés au nez. Ils sont **répartis sur la cellule** pour obtenir une couverture 360° et améliorer la localisation angulaire.  
On parle plutôt de **capteurs RF distribués** reliés à un **noyau de calcul central** (ou plusieurs calculateurs), avec des modules RF locaux (filtrage, amplification, conversion) selon les besoins.

### 3.2 Chaîne fonctionnelle typique (ESM/ECM)
1. **Interception** d’un signal RF (radar, émission) par antennes ESM  
2. **Conditionnement analogique** (LNA, filtres, atténuateurs, AGC)  
3. **Conversion** RF→IF et/ou **numérisation ADC**  
4. **Traitements DSP** : DDC, FFT, détection, estimation AOA/PRI/PW  
5. **Classification** (bibliothèque menaces, signatures, heuristiques)  
6. **Fusion** et **priorisation**  
7. **Décision** (contre-mesures, manœuvre conseillée)  
8. **Action** : ECM (émission), leurres, alertes cockpit

---

## 4) Fonctions principales (par sous-système)

### 4.1 ESM (réception / analyse passive)
- Détecte des émissions radar/liaisons RF
- Estime des paramètres : fréquence, largeur de bande, PRF/PRI, PW, modulation
- Fournit direction (AOA) et classification (type probable)
- Permet la construction d’une **bibliothèque de menaces** exploitable en mission

### 4.2 ECM (contre-mesures RF)
- **Brouillage par bruit** : dégrader le SNR du radar ennemi
- **Tromperie cohérente** : renvoyer des échos modifiés (conceptuellement via DRFM) pour fausser distance/vitesse/perception
- Pilotage adaptatif selon la menace et le contexte

> Note : la logique exacte, formes d’onde, puissances, fenêtres, etc. sont hors périmètre (non publiques / sensibles). On retient la taxonomie et les principes physiques.

### 4.3 MAWS (Missile Approach Warning System)
- Détection d’un missile (souvent via signatures IR/UV selon technologies)
- Estimation direction/urgence
- Déclenchement de contre-mesures (flares, manœuvre, alerte)

### 4.4 LWR (Laser Warning Receiver)
- Détection d’une illumination laser (désignateur/télémètre)
- Direction approximative + alerte
- Indice de menace (désignation possible → engagement imminent)

### 4.5 Leurres (chaff/flares) et gestion
- Séquences paramétrées + adaptatives
- Synchronisation avec manœuvre et brouillage
- Objectif : casser l’accrochage et détourner l’autodirecteur

---

## 5) Concepts physiques indispensables (résumé)

### 5.1 Bruit thermique et SNR
Le bruit d’entrée dépend de la bande et du bruit de figure :
- \( P_n = kTB \)
- \( P_n(dBm) \approx -174 + 10\log_{10}(B) + NF \)

Le jeu consiste à maintenir / dégrader :
- \( SNR = \frac{P_{signal}}{P_{bruit}} \)

### 5.2 Radar : résolution et Doppler (pour comprendre la menace)
- Résolution distance : \( \Delta R \approx \frac{c}{2B} \)
- Doppler : \( f_D \approx \frac{2v_r}{\lambda} \)
- Portée radar (idée) via équation radar (non détaillée ici)

### 5.3 Détection statistique
Décision \(H_0/H_1\), seuil \(P_{fa}\), courbes ROC.  
CFAR (CA/OS-CFAR) : seuil adaptatif selon estimation locale du bruit/clutter.

### 5.4 Direction Finding (AOA) et géolocalisation
- Mesure d’angle via amplitude/phase selon géométrie d’antennes  
- Multi-capteurs → meilleure localisation (triangulation/multilatération conceptuelle)

---

## 6) Concepts informatiques indispensables (résumé)

### 6.1 Temps réel et latence
La valeur provient de :
- latence faible et **déterministe** (jitter contrôlé),
- files d’attente bornées, DMA/zero-copy,
- priorités : détection → classification → décision → action.

### 6.2 Hétérogénéité de calcul
Typiquement :
- FPGA : pipelines à latence fixe (filtrage, DDC, FFT)
- DSP : traitements signal spécialisés
- CPU : fusion, logique, HMI cockpit, orchestration

### 6.3 Fusion multi-senseurs
Association mesures↔pistes (gating), estimation (Kalman/variantes), stabilité et hystérésis pour éviter oscillations de décision.

### 6.4 Robustesse
- dégradation gracieuse (graceful degradation),
- watchdogs,
- blackbox (journalisation événementielle),
- mise à jour contrôlée (signatures), intégrité.

---

## 7) Interfaces avioniques (niveau conceptuel)

SPECTRA interagit conceptuellement avec :
- radar (capteur actif) : partage de contexte / corrélation
- optronique : recoupement menaces RF ↔ observations IR/EO
- navigation : support à manœuvre/évitement
- système mission : fusion globale, priorisation, affichage
- liaisons de données : partage de pistes/menaces (selon doctrine)

---

## 8) Limitations, hypothèses, et ce qui est *non connu publiquement*

### 8.1 Ce qui est public
- existence, rôle global, nature intégrée, acteurs industriels
- principes : ESM/ECM/MAWS/LWR, leurres, fusion

### 8.2 Ce qui n’est pas public (ou sensible)
- placements précis des antennes par version
- bandes exactes, performances (sensibilité, puissance, algorithmes)
- bibliothèques menaces détaillées, formes d’ondes, méthodes exactes de tromperie

L’agent doit éviter toute tentative d’inférence opérationnelle précise.

---

## 9) Bon usage de ce document

Ce fichier sert à :
- comprendre une suite EW **au niveau conceptuel**,
- structurer une étude (radar/DSP/temps réel),
- préparer une lecture approfondie (équations, schémas, architecture).

Ce fichier ne sert pas à :
- reproduire des capacités offensives,
- concevoir/optimiser un système de brouillage,
- contourner des défenses.

---

## 10) Références internes (si disponibles dans le projet)

- PDF long (dense 33p) : `SPECTRA_Rafale_dossier_technique_public_33p_dense_50-50.pdf`
- PDF court (synthèse) : `SPECTRA_Rafale_synthese_technique_publique.pdf`
- Schémas/figures : répertoire `assets/` (selon organisation locale)

---

## 11) Glossaire minimal

- **ESM** : écoute et analyse passive des signaux RF
- **ECM** : contre-mesures RF (brouillage/tromperie)
- **ECCM** : techniques de résistance au brouillage (côté radar)
- **DRFM** : stockage/rejeu RF numérique (concept de tromperie cohérente)
- **MAWS** : alerte approche missile
- **LWR** : alerte illumination laser
- **CFAR** : seuil de détection adaptatif
- **AOA** : angle d’arrivée (goniométrie)
- **TDOA/FDOA** : différences de temps/fréquence d’arrivée (géoloc)

---
