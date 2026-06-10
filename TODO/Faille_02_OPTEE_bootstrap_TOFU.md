# Faille 02 — Bootstrap OP-TEE session-auth en Trust-On-First-Use non authentifié

## Priorité : **P0 — Critique** · Gravité : ~8.7

## Localisation
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c:1267-1329` (`verify_session_auth_or_bootstrap`), lignes 1316-1322 spécifiquement
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c:69-70` (constantes magic)
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c:1185-1203` (`set_session_auth_secret_cmd`)
- `drone_transition_source/optee_ta_skeleton/host/main.c:282-294` (constantes magic répétées côté host)

## Description
Lorsque le TA démarre vierge (pas encore de `session_auth_hash` stocké), il accepte l'installation d'un nouveau secret à condition que `params[1].value.a == HESIA_BOOTSTRAP_MAGIC_A` (`0x48455349` = `"HESI"`) et `params[1].value.b == HESIA_BOOTSTRAP_MAGIC_B` (`0x41555448` = `"AUTH"`).

```c
#define HESIA_BOOTSTRAP_MAGIC_A 0x48455349u /* HESI */
#define HESIA_BOOTSTRAP_MAGIC_B 0x41555448u /* AUTH */
```

**Ces constantes sont publiques**, hard-codées en clair à la fois dans le TA et dans le host tool. Aucune signature, aucune attestation, aucune vérification de l'origine (UID REE, TEEC_LOGIN_APPLICATION, UUID client) n'est exigée. Le seul requis est l'accès à `/dev/tee0`, accordé à tout user du groupe `tee` (cf. `drone_source/optee_client.cpp:528`).

## Impact
Sur un device vierge (premier déploiement) ou après un wipe (cf. Faille_01 + Faille_11), **tout process REE dans le groupe `tee`** peut bootstrap un secret de 32 octets arbitraires et devient owner perpétuel du TA.

Conséquences :
1. L'attaquant peut rotater le secret à volonté ensuite (une fois bootstrapé, la rotation n'exige que la session auth courante).
2. L'attaquant verrouille le drone : l'opérateur légitime ne peut plus provisioner un nouveau secret sans passer par un recovery (Faille_13).
3. Combiné à Faille_01, l'attaquant peut rester owner indéfiniment même après tentative de wipe par l'opérateur.

## Scénario d'exploitation
```c
// Attaquant REE, user dans le groupe tee, device fraîchement flashé
TEEC_Session sess;
TEEC_Operation op = {0};
uint8_t malicious_secret[32];
RAND_bytes(malicious_secret, 32); // l'attaquant choisit son secret
op.params[0].tmpref.buffer = malicious_secret;
op.params[0].tmpref.size = 32;
op.params[1].value.a = 0x48455349; // HESI
op.params[1].value.b = 0x41555448; // AUTH
op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE);
TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC, NULL, &op, NULL);
// -> l'attaquant a bootstrap avec son secret
```

## Correctif recommandé
1. **Supprimer les constantes magic statiques** `HESIA_BOOTSTRAP_MAGIC_A/B`. Elles n'apportent aucune protection cryptographique.
2. **Exiger un token de provisioning signé** par `kHesiaRecoveryPubkey` (déjà embarquée ligne 72-79 du TA) :
   - Token structure : `(device_uuid || timestamp || nonce_challenge || new_secret_hash)`
   - Signature ECDSA P-256 par la clé de recovery **offline**
   - Le TA vérifie la signature, la fraîcheur du timestamp, l'unicité du nonce
3. **Alternative** : exiger `TEEC_LOGIN_APPLICATION` + UUID client vérifié contre un allowlist embarqué dans le TA (sans fuite via SFS).
4. **Marqueur factory-once** : stocker dans un objet SFS anti-rollback un bit "already_provisioned_once" qui empêche toute re-bootstrap sans token recovery.

## Dépendances
- Faille_01 : sans RPMB, même le bit "already_provisioned_once" peut être rollback. Les deux corrections sont couplées.
- Faille_11 : `wipe_key_internal` partiellement cassé, laisse un device partiellement wipé exposé au bootstrap.

## Jetson requis
Oui, voir `ACCES_JETSON_REQUIS.md` §7 pour PoC.

## Effort estimé
- 1 à 2 semaines de dev TA + host tool
- Ceremony de génération/custody de la clé recovery offline ajoutée : 1 semaine organisationnelle
