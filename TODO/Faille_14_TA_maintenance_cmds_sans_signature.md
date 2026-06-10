# Faille 14 — Commandes de maintenance TA sans vérification de signature

## Priorité : **P1 — Haute** · Gravité : ~6.7

## Localisation
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c` :
  - Table dispatch `TA_InvokeCommandEntryPoint` (≈ lignes 2450-2650)
  - Commandes sensibles :
    - `CMD_RESET_SLOT_META`
    - `CMD_FORCE_FW_VERSION`
    - `CMD_DEBUG_DUMP_STATE`
    - `CMD_CLEAR_ALL`
    - `CMD_SET_POLICY_BYPASS`
  - Fonction `verify_session_auth_or_bootstrap` (1267-1329)

## Description
Plusieurs commandes administrateur du TA sont gatées **uniquement** par `session_auth_hash` (un secret de 32 octets installé au bootstrap). Elles n'exigent pas de signature asymétrique par une clé offline operator.

Conséquence : quiconque détient le `session_auth_secret` actuel peut :
- Forcer un downgrade de `fw_version`.
- Écraser `slot_meta` (A/B boot).
- Dumper l'état en mode debug (voire accéder aux plaintext sealées).
- Désactiver des policies.

Or, `session_auth_secret` est manipulé côté host (`drone_source/optee_client.cpp`, `hesia_drone.cpp`, `drone_transition_source/scripts/provision_optee_session_auth.sh`) et circule via fichier texte en base64. Sa compromission transforme l'attaquant en super-admin TA.

## Impact
- **Escalade de privilèges TA** : une fuite du `session_auth_secret` (vol de fichier, fuite d'un ops engineer, log qui le capture) donne pleins pouvoirs sur le TA.
- **Pas de principle of least privilege** : une commande de simple rotation et une commande de wipe utilisent le même credential.
- **Pas de 2-of-N** : aucune commande ne requiert deux signatures (op + security officer).

## Scénario d'exploitation
1. Attaquant obtient le fichier `/etc/hesia/session_auth.b64` (lisible par root, gardé mais présent sur le drone).
2. Il connecte le TA via son REE local compromis.
3. Injecte `CMD_FORCE_FW_VERSION` avec version ancienne vulnérable.
4. Reboot → firmware vulnérable actif.

## Correctif recommandé
1. **Classifier les commandes** en 3 niveaux :
   - L1 (runtime) : session_auth seule suffit.
   - L2 (maintenance) : signature ECDSA P-256 par `kHesiaOperatorPubkey` obligatoire en plus.
   - L3 (critique : wipe, force fw downgrade, policy change) : signature 2-of-N par `kHesiaOperatorPubkey` ET `kHesiaRecoveryPubkey`.
2. **Structure de token** :
   ```c
   struct cmd_token {
       uint32_t cmd_id;
       uint64_t timestamp;
       uint8_t  challenge_nonce[16]; // fourni par TA au préalable
       uint8_t  payload_hash[32];
       uint8_t  signature[64];       // ECDSA P-256
   };
   ```
3. **Nonce challenge** : TA émet un nonce valide 5 minutes, opérateur le signe offline, rejette hors fenêtre.
4. **Audit log append-only** pour L2/L3.
5. **Déprécier `session_auth` comme seul guardien** des commandes critiques.

## Dépendances
- Faille_13 : similitude avec recovery_nonce, structure commune souhaitée.
- Faille_02 : bootstrap TOFU facilite la prise de contrôle initiale.

## Jetson requis
Oui pour test end-to-end.

## Effort estimé
- 2 à 3 semaines dev TA + host + 1 semaine ceremony clés opérateur.
