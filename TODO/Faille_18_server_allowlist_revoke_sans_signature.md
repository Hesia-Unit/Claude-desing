# Faille 18 — Serveur : allowlist et révocation sans signature

## Priorité : **P1 — Haute** · Gravité : ~7.4

## Localisation
- `server_source/src/main.cpp` : chargement config runtime
- `server_source/src/hesia_server_session.cpp` :
  - Lecture fichier `allowed_devices.json` (allowlist attestation pubkey)
  - Lecture fichier `revoked_devices.json`
- `server_source/tools/rotate_all_keys.sh` : gère rotation mais pas signature côté allowlist

## Description
Le serveur HESIA maintient deux fichiers sur disque :
- `allowed_devices.json` : mapping `device_id → attest_pub_sha256` accepté.
- `revoked_devices.json` : liste de `device_id` ou `attest_pub_sha256` à rejeter.

Ces fichiers sont **lus en texte clair** (JSON) sans signature ni MAC. Les garde-fous :
- Permissions UNIX `0600 hesia:hesia` (posées dans systemd unit).
- Pas de checksum en mémoire.
- Pas de vérification à chaud (un editor qui écrit entre deux lectures peut créer une incohérence).

## Impact
- **Compromission disque = compromission policy** : un attaquant avec write access au fichier peut :
  - Ajouter un `device_id` clone (Faille_12).
  - Retirer un `device_id` légitime (DoS).
  - Retirer un drone révoqué de la liste `revoked` → réactivation silencieuse.
- **Race condition** : lecture partielle si fichier modifié pendant parse.
- **Pas d'audit trail** : aucun log de qui a modifié la config à quel moment.

## Scénario d'exploitation
Attaquant ayant obtenu un RCE low-priv ou un accès via vulnérabilité OS :
1. Obtient `sudo` ou un CVE d'escalade.
2. Édite `allowed_devices.json` pour ajouter son clone.
3. Le serveur relira à la prochaine rotation (ou au prochain handshake selon impl).
4. Son clone est accepté sans alerte.

## Correctif recommandé
1. **Signer `allowed_devices.json`** avec une clé Ed25519 "policy-server" offline :
   ```json
   {
     "schema": "hesia-allowlist-v1",
     "updated_at": "2026-04-24T12:00:00Z",
     "entries": [...],
     "signature": "base64(...)",
     "signer_pubkey_id": "policy-2026-q2"
   }
   ```
2. **Vérifier la signature** à chaque chargement. Refuser si mismatch ou signature ancienne > 30 jours.
3. **Stocker la policy dans un SGX/TPM** ou un HSM réseau si disponible.
4. **Journal append-only** des modifications (`allowlist_audit.log`), signé lui-même.
5. **Double-source** : comparer la policy locale avec un miroir distant (serveur de révocation) ; refuser si incohérent.
6. **Permissions inode** : `chattr +i` + `setcap` pour que seul un processus de maintenance identifié puisse écrire.

## Dépendances
- Faille_12 : clé d'attestation non HW-bound → l'allowlist est notre seule défense côté serveur.
- Faille_22 : API UI si exposée peut muter la liste.

## Jetson requis
Non.

## Effort estimé
- 1 semaine dev serveur + host tool + 3 jours tests.
