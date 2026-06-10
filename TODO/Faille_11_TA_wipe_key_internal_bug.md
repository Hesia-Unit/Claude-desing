# Faille 11 — `wipe_key_internal` efface partiellement l'état TA

## Priorité : **P1 — Haute** · Gravité : ~6.8

## Localisation
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c` :
  - `wipe_key_internal()` (≈ lignes 1396-1450)
  - `wipe_all_state_cmd()` (≈ lignes 1460-1495)

## Description
La fonction `wipe_key_internal()` a pour objectif d'effacer l'état confidentiel du TA (clé(s) de session ML-KEM, session_auth, etc.) à la demande de l'opérateur. L'inspection du code révèle :
- Efface bien : `session_auth_hash`, `session_auth_secret_ephemeral` en RAM.
- **N'efface pas** :
  - `slot_meta` persistant
  - `fw_version` persistant
  - `attest_priv` persistant (mais efface `attest_pub_cache`)
  - `mldsa_keyblob` persistant
  - `recovery_nonce`
  - Les objets SFS sur disque (uniquement en RAM)
- Utilise `memset(ptr, 0, n)` sans `explicit_bzero` ou `OPENSSL_cleanse` → le compilateur peut élider les stores (seul GCC/clang avec `-O2` sur TA build).

## Impact
- **Fausse promesse de wipe** : un opérateur qui invoque `wipe_all_state` pense avoir effacé le drone, mais `attest_priv` reste et permet toujours au drone de signer des attestations → serveur ne détecte pas le changement.
- **Fenêtre de re-bootstrap dangereuse** (Faille_02) : après wipe, le drone accepte un nouveau bootstrap, mais conserve ses anciennes clés d'attestation → attaquant peut fabriquer un drone "propre" qui conserve l'identité historique.
- **Risque RMA** : un drone retourné pour maintenance "wipé" peut être réintroduit dans le parc avec son identité intacte, compromettant la traçabilité.

## Scénario d'exploitation
1. Opérateur demande `wipe_all_state` suite à suspicion de compromission.
2. Attaquant (owner TA via Faille_02) observe que seuls les champs RAM ont été effacés.
3. Re-bootstrap du TA avec un nouveau secret attaquant.
4. L'attaquant peut signer avec l'ancienne `attest_priv` et se faire passer pour le drone d'origine côté serveur.

## Correctif recommandé
1. **Audit exhaustif des objets persistants** à effacer dans `wipe_key_internal` :
   ```c
   static const char* wipe_list[] = {
       "session_auth_v1",
       "slot_meta_v1",
       "fw_version_v1",
       "attest_priv_v1",
       "mldsa_keyblob_v1",
       "recovery_nonce_v1",
       NULL
   };
   ```
2. Boucler sur cette liste, ouvrir chaque objet, écraser avec random, puis `TEE_CloseAndDeletePersistentObject`.
3. **Utiliser `explicit_bzero` / `OPENSSL_cleanse`** pour les effacements RAM.
4. **Journaliser le wipe** via `tee_log_commit_event("WIPE_COMPLETE", ...)`.
5. **Marqueur "wiped"** : écrire un objet `"state_is_wiped"` persistant, vérifié au chargement du TA pour refuser toute opération jusqu'à re-provisioning complet.
6. **Test d'intégration** : après wipe, toute commande sauf bootstrap/provision doit retourner `TEE_ERROR_NOT_SUPPORTED`.

## Dépendances
- Faille_02 : bootstrap TOFU exploitable post-wipe partiel.
- Faille_12 : attest_priv orphelin.
- Faille_01 : sans RPMB, même un wipe parfait est réversible par snapshot.

## Jetson requis
Oui pour validation (exécution TA + inspection /data/tee/).

## Effort estimé
- 4 à 6 jours dev TA + 3 jours tests de régression.
