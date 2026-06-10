# Faille 15 — `sign_attest_digest_cmd` accepte un digest < 32 octets

## Priorité : **P1 — Haute** · Gravité : ~6.0

## Localisation
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c` :
  - `sign_attest_digest_cmd()` (≈ lignes 520-580)

## Description
La commande `sign_attest_digest_cmd` prend en entrée un buffer `params[0].tmpref` censé contenir un SHA-256 de la structure d'attestation (32 octets). Le code vérifie :
```c
if (params[0].tmpref.size < 1 || params[0].tmpref.size > 64) {
    return TEE_ERROR_BAD_PARAMETERS;
}
```

**Problèmes** :
1. La borne basse est 1 octet et non 32. Un attaquant peut passer un "digest" de 1 à 31 octets.
2. Le signe ECDSA P-256 est ensuite calculé sur `digest || zero_padding` implicite (ou sur le buffer brut selon l'impl OP-TEE) → la signature reste **mathématiquement valide** mais le contenu sémantique est ambigu.
3. Le serveur, recevant `(digest, signature)`, peut rejeter ou accepter selon son propre parsing. Si le serveur accepte, un attaquant forge une attestation arbitraire avec très peu d'entropie à deviner (ex : digest 2 octets = 65536 possibilités).

## Impact
- **Attestations faibles** : un attaquant qui contrôle la commande TA peut produire des signatures sur des digests arbitrairement courts, contournant les extensions d'attestation (timestamp, boot_counter, measured_boot_manifest).
- **Divergence parseur** (parser differential) : serveur peut accepter ce que l'auditeur tiers n'accepterait pas.

## Scénario d'exploitation
1. Attaquant compromis REE/session_auth envoie `sign_attest_digest` avec un buffer de 2 octets.
2. TA signe sans objection.
3. Serveur (selon impl) peut accepter la signature comme attestation valide d'un état partiel.

## Correctif recommandé
1. **Fixer strictement la taille attendue à 32 octets** (SHA-256) ou 48 (SHA-384) :
   ```c
   if (params[0].tmpref.size != 32) {
       return TEE_ERROR_BAD_PARAMETERS;
   }
   ```
2. **Pré-hasher côté TA** : plutôt que de recevoir un digest, recevoir les champs structurés (boot_counter, fw_version, timestamp, challenge) et hasher dans le TA — cela évite tout risque de forge par digest crafted.
3. **Ajouter un domain separator** : SHA-256(`"HESIA-ATTEST-v1" || device_uuid || payload`) plutôt que SHA-256(payload).
4. **Rejeter explicitement** les digests de longueur non-standard côté serveur aussi.

## Dépendances
- Faille_12 : clé d'attestation P-256 non HW-bound → aggravation si signature forge.
- Faille_14 : commandes maintenance sans signature facilitent l'accès.

## Jetson requis
Recommandé pour reproduction.

## Effort estimé
- 2 jours dev TA + 2 jours tests de régression + coordination serveur.
