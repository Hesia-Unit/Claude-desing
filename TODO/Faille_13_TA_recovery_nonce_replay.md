# Faille 13 — Rejeu de `recovery_nonce` via rollback SFS

## Priorité : **P1 — Haute** · Gravité : ~6.3

## Localisation
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c` :
  - `generate_recovery_nonce_cmd` (≈ lignes 540-558)
  - `consume_recovery_nonce_cmd` (≈ lignes 560-605)
  - `clear_recovery_nonce_cmd` (≈ lignes 607-630)

## Description
Le `recovery_nonce` est un nonce 32 octets utilisé dans le protocole de recovery (reset d'urgence du drone). Flux prévu :
1. Opérateur demande `generate_recovery_nonce` → TA génère un nonce random, le seal et le stocke persistant.
2. Opérateur présente au TA une preuve signée par `kHesiaRecoveryPubkey` incluant ce nonce + nouvelle config.
3. TA vérifie la signature, puis `consume_recovery_nonce` efface le nonce pour empêcher rejeu.

**Problèmes** :
1. **Aucune ancre anti-rollback** : le nonce est stocké dans SFS, un attaquant snapshot + restore (Faille_01) peut le "ressusciter" après consommation.
2. **Pas de compteur monotone associé** : `recovery_nonce_version` si présent, n'est pas couplé à une fuse.
3. **Fenêtre de validité non bornée** : `generate` peut être appelé à J0, `consume` à J0+6 mois avec le même nonce.

## Impact
- **Rejeu de recovery** : un attaquant qui capture un recovery-token signé légitime + snapshot du SFS au moment de sa génération peut le rejouer après que l'opérateur l'a consommé.
- Combiné à Faille_02 : attaquant peut revenir en arrière puis bootstrap.

## Scénario d'exploitation
1. Opérateur demande `generate_recovery_nonce` à J0.
2. Attaquant root REE snapshot `/data/tee/` immédiatement après.
3. Opérateur envoie la preuve signée, TA consomme.
4. Attaquant restore → le nonce est disponible à nouveau.
5. Attaquant rejoue la preuve signée déjà capturée (le serveur de recovery ne détecte pas puisque le drone l'accepte à nouveau).

## Correctif recommandé
1. **Compteur monotone recovery_counter** stocké en fuse OTP, incrémenté à chaque `consume`. Le TA refuse tout nonce d'une époque antérieure.
2. **Timestamp de validité** : le nonce inclut une date d'expiration (ex : 24h), TA refuse après.
3. **Unicité par device_id + tegra_chip_id** : le nonce inclut l'identité hardware pour empêcher un nonce d'un device A d'être utilisé sur un device B.
4. **Audit log** : chaque consume loggé dans un append-only monotone.
5. **Dépendance Faille_01** : sans RPMB ou fuse, la correction est palliative.

## Dépendances
- Faille_01 : RPMB/fuse nécessaires.
- Faille_02 : bootstrap TOFU facilite les chaînes d'attaque.
- Faille_14 : les commandes de maintenance associées ne sont pas signées.

## Jetson requis
Oui (reproduction sur TA réel).

## Effort estimé
- 1 à 2 semaines dev TA + 3 jours tests rejeu.
