# Faille 06 — Sealing TA AES-GCM sans AAD de domaine

## Priorité : **P1 — Haute** · Gravité : ~7.5

## Localisation
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c` :
  - `seal_blob_to_object` (≈ lignes 910-960)
  - `unseal_blob_from_object` (≈ lignes 962-1018)
  - Appels depuis `store_session_auth_secret` (1144-1162), `store_slot_meta` (760-805), `store_fw_version` (2210-2247), `store_attest_priv` (444-508), `store_mldsa_keyblob` (1629)

## Description
Le TA utilise une fonction `seal_blob_to_object()` qui applique AES-256-GCM à chaque blob persistant stocké dans le Secure File System (SFS). L'IV dérive d'une HUK TA + filename, la clé de sealing dérive aussi de la HUK.

**Problème** : l'AAD de la construction GCM n'inclut **pas** :
- Le nom du domaine (ex : `"session_auth_v1"`, `"slot_meta_v1"`, `"fw_version_v1"`)
- L'UUID du TA
- Le type d'objet attendu

Concrètement, un attaquant ayant un accès fichier au SFS (/data/tee/) peut **renommer** un blob `session_auth_hash` en `attest_priv_key` ou réciproquement. Le GCM ne rejettera pas le déchiffrement car l'AAD est inexistant (ou uniquement l'IV qui dépend du nom de fichier mais seulement pour l'unicité, pas pour l'authentification du domaine).

Ce défaut est aggravé par l'absence de RPMB (Faille_01) : l'attaquant peut restaurer un état antérieur et/ou croiser des blobs entre domaines.

## Impact
- **Cross-domain confusion** : un blob sealé pour un usage A peut être déchiffré avec succès comme usage B.
  - Si `attest_priv` (32 octets) et `session_auth_hash` (32 octets) ont la même taille et le même chemin de dérivation, renommer l'un en l'autre permet de substituer la clé d'attestation.
- **Type confusion** : un blob de 512 octets (ML-DSA keyblob segment) pourrait être présenté comme un payload de `sign_attest_digest` si la longueur convient.
- **Faille de sécurité cumulative** : en combinant Faille_01 (rollback) + Faille_06 (cross-domain) + Faille_11 (wipe partiel), l'attaquant peut créer des états incohérents où le TA accepte des secrets déjà révoqués.

## Scénario d'exploitation
```
# attaquant root REE
cp /data/tee/<uuid_session_auth> /data/tee/<uuid_attest_priv>
# Le TA, sollicité pour signer avec attest_priv, déchiffre en fait session_auth_hash
# -> signature inutile mais le TA ne remarque rien
# Variante : substituer un slot_meta par un fw_version pour casser l'anti-rollback
```

## Correctif recommandé
1. **Ajouter un domain separator dans l'AAD de chaque sealing** :
   ```c
   uint8_t aad[64];
   size_t aad_len = snprintf(aad, sizeof(aad), "HESIA|TA|%s|v1", domain);
   EVP_EncryptUpdate(ctx, NULL, &dummy, aad, aad_len);
   ```
2. **Inclure l'UUID TA et la version de schéma** dans l'AAD.
3. **Inclure la longueur attendue** du plaintext dans l'AAD : `aad += htobe32(expected_plain_len)`.
4. **Test de régression** : écrire un KAT qui vérifie que `seal("domainA", ...)` ne se déchiffre pas avec `unseal("domainB", ...)`.
5. **Magic header** : préfixer le plaintext d'un magic 8 octets par domaine (`"SAUTH001"`, `"SLOTM001"`, etc.), que le code vérifie après déchiffrement.

## Dépendances
- Faille_01 : sans RPMB, la cross-substitution est durable (persiste au reboot).
- Faille_11 : `wipe_key_internal` n'efface pas tous les blobs → fenêtre exploitable post-wipe.

## Jetson requis
Oui pour reproduction dynamique (cf. `ACCES_JETSON_REQUIS.md` §7). Analyse statique possible mais confirmation d'exploitation requiert le TEE réel.

## Effort estimé
- 1 semaine de dev TA + 3 jours tests KAT cross-domain.
