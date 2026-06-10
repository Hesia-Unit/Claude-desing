# Faille 01 — Absence de RPMB : rollback complet de l'état TEE

## Priorité : **P0 — Critique** · Gravité : ~8.2 (Critique)

## Localisation
- Plateforme cible : Jetson Orin Nano Super SD-boot
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c`
  - `session_auth_hash` : ligne 1144-1162
  - `slot_meta` : ligne 760-805
  - `fw_version` : ligne 2210-2247
  - `recovery_nonce` : ligne 540-558
  - `attest_priv/pub` : ligne 444-508
  - `mldsa_keyblob` : ligne 1629
- Policy documentée : `security/policies/jetson_orin_nano_super_runtime.policy.conf`
  (`drone.require_rpmb_rollback_storage=0`)

## Description
Le Jetson Orin Nano Super SD-boot **n'expose pas de partition RPMB**. Tous les objets persistants OP-TEE (`TEE_STORAGE_PRIVATE`) vivent dans `/data/tee/` côté REE. Ils sont chiffrés par la clef HUK du TA, **mais ne sont pas anti-replay** (aucune ancre matérielle monotone).

Un attaquant root REE peut :
1. snapshoter `/data/tee/` à l'instant T
2. laisser le TA opérer (rotation de session-auth, incrément fw_version, commit A/B slot, consommation recovery nonce)
3. restaurer à T+N → toutes les protections anti-rollback sont réinitialisées

## Impact
1. **Session-auth downgrade** : secret révoqué par opérateur redevient valide.
2. **Firmware rollback** : `check_version_cmd` (ta_hesia.c:2249-2268) applique l'anti-rollback en mémoire mais écrit dans un SFS réversible → firmware vulnérable ancien peut être réadmis.
3. **Slot A/B replay** : `commit_slot_boot_cmd` (ta_hesia.c:2349) refuse `req.firmware_version < meta.max_firmware_version`, mais restauration du SFS fait baisser `max_firmware_version` → slot obsolète peut rebooter.
4. **Recovery nonce replay** : même avec clear_recovery_nonce, la restauration du SFS recrée un nonce précédemment consommé.
5. **Clé d'attestation orpheline** : (cf. Faille_12) si l'état attestation est restauré, une clé publique déjà révoquée côté serveur pinner redevient la clé active du drone.

L'ensemble des garanties revendiquées dans `SECURITY_HARDENING.md` points 5-8 est caduc sans RPMB ou équivalent fuse-bound.

## Scénario d'exploitation
**Préconditions** : attaquant ayant un root shell sur le REE du drone (local ou via compromission d'une chaîne applicative).

```
# attaquant, root REE
tar -czf /tmp/snapshot.tgz /data/tee/
# ... opérateur rotate session-auth / push new firmware ...
tar -xzf /tmp/snapshot.tgz -C /
systemctl restart hesia-drone.service
# le TA a retrouvé son ancien état : session-auth antérieur, fw_version antérieur
```

Combiné à Faille_02 (bootstrap TOFU) : cycle d'attaque complet sans reset matériel.

## Correctif recommandé
### Court terme (workarounds, palliatifs)
- Ancrer les compteurs monotones (`fw_version`, `slot_meta.version`, `attest_state_version`, `session_auth_version`) dans les fuses OTP on-die du Jetson (Tegra security fuses écrivables ~1000 fois).
- À défaut, calculer `MAC(HUK_device, monotonic_counter_fuse)` et refuser toute lecture dont le compteur persistent est strictement inférieur à la valeur fuse.
- Durcir les permissions `/data/tee/` à `0700 root:root` avec `chattr +i` sur les inodes critiques (ne bloque pas un attaquant root mais ralentit).

### Long terme (structurel)
- Migrer vers un Jetson module eMMC avec RPMB exploitable.
- Ou intégrer un secure element externe (i2c, SPI) contenant un counter monotone et une clé sign-only.
- Réviser la `SecurityPolicy` pour refuser toute charge du TA sans preuve de `require_rpmb_rollback_storage=1`.

## Dépendances
- Faille_02 : bootstrap TOFU → complète le cycle attaque.
- Faille_06 : sealing AES-GCM sans AAD → facilite confusion cross-domaine lors du rollback.

## Jetson requis
Oui, voir `ACCES_JETSON_REQUIS.md` §2 pour reproduction effective.

## Effort estimé
- Workaround (fuse monotone + MAC) : **3 à 4 semaines** dev + 1 semaine de tests.
- Migration hardware eMMC : **1 à 2 trimestres** (revoir BOM, supply chain, certifier).
