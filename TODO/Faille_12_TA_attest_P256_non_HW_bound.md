# Faille 12 — Clé d'attestation P-256 non ancrée matériellement

## Priorité : **P1 — Haute** · Gravité : ~6.5

## Localisation
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c` :
  - `generate_attest_keypair_cmd` (≈ lignes 444-508)
  - `sign_attest_digest_cmd` (≈ lignes 520-580)
  - `export_attest_pubkey_cmd` (≈ lignes 510-518)

## Description
Le TA génère une paire ECDSA P-256 "d'attestation" avec `TEE_GenerateKey(TEE_TYPE_ECDSA_P256, ...)`. La clé privée est ensuite sealée et stockée dans le SFS (`seal_blob_to_object`).

**Problèmes** :
1. **Pas de fuse-binding** : la clé privée n'est pas ancrée dans les fuses du Jetson Orin Nano Super. Elle vit uniquement dans `/data/tee/` SFS (sealé par la HUK, mais la HUK elle-même est dérivée de la tegraid si je lis les specs Tegra correctement).
2. **Pas de certificat matériel (EK-like)** : il n'existe pas de chaîne de certification hardware-vendor → device_id → attestation_key, contrairement à Android StrongBox ou iOS SEP.
3. **Génération en RAM puis sealing** : la clé transite par la RAM du TA, potentiellement exposée via side-channels SMC durant la génération.
4. **Pas de certification par le fabricant** : le serveur qui reçoit `export_attest_pubkey_cmd` doit avoir une méthode pour associer `device_id ↔ attest_pub` via un canal side-channel (allowlist signée), ce qui ramène la confiance à la bonne gestion de cette allowlist.

## Impact
- **Clone de device** : un attaquant qui extrait `/data/tee/<attest_priv>` (requiert un bypass du sealing, cf. Faille_06) peut cloner l'identité d'attestation d'un drone.
- **Pas de preuve de possession matérielle** : le serveur ne peut pas distinguer un vrai drone d'un émulateur TA correctement équipé de la même HUK.
- **Renouvellement destructif** : régénérer la clé d'attestation invalide toutes les attestations historiques mais ne garantit pas que l'ancienne clé ne sera pas exploitée en rollback (Faille_01).

## Scénario d'exploitation
1. Attaquant ayant un accès racine REE + TEE extrait le SFS complet.
2. Extract `attest_priv` via Faille_06 (cross-domain confusion) ou attaque sur la HUK (si HUK statique dérivée de tegraid publiable).
3. Monte un émulateur OP-TEE sur un autre Jetson avec la même HUK (ou patche le TA pour charger `attest_priv` extrait).
4. Ce clone peut signer des attestations acceptées par le serveur comme "drone légitime".

## Correctif recommandé
1. **Utiliser une PKCS#11 hardware** via un secure element SPI/I2C si disponible. À défaut :
2. **Ancrer la clé dans une fuse monotone** : la clé privée est générée au premier boot et son SHA-256 est écrit dans une fuse OTP → impossibilité de régénération silencieuse.
3. **Vérifier fuse au chargement** : à chaque démarrage, le TA lit la fuse et refuse de charger toute `attest_priv` dont le hash ne correspond pas.
4. **Ajouter une identité matérielle** : dériver `attest_priv = HKDF(HUK || tegra_chip_id || OTP_nonce, "attest_v1")` pour empêcher clone cross-device.
5. **Chaîne de certification** : signer la pubkey émise par une clé manufacturer (`kHesiaFactoryPubkey`) au moment du provisioning usine.
6. **Rotation synchronisée** : toute nouvelle génération `attest_priv` incrémente un `attest_version_fuse` et invalide les clés antérieures côté serveur via révocation active.

## Dépendances
- Faille_01 : RPMB → sans, rotation reversible.
- Faille_06 : cross-domain confusion permet l'extraction.
- Faille_11 : wipe partiel laisse `attest_priv` intact.

## Jetson requis
Oui (cf. `ACCES_JETSON_REQUIS.md` §2 et §7).

## Effort estimé
- 2 à 3 semaines dev TA + host + 1 semaine ceremony usine de signature.
