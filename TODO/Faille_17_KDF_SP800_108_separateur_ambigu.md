# Faille 17 — KDF SP 800-108 label/context sans séparateur clair

## Priorité : **P1 — Haute** · Gravité : ~5.8

## Localisation
- `drone_source/kdf_sp800_108.cpp` :
  - `kdf_sp800_108_hmac_sha256()` (tout le fichier)
- Call sites : `drone_source/hesia_drone.cpp`, `drone_source/secure_channel.cpp` (si activé), `server_source/src/hesia_server_session.cpp`

## Description
L'implémentation SP 800-108 (mode counter, HMAC-SHA-256) construit l'input HMAC comme :
```
i || Label || 0x00 || Context || [L]_32
```
Classique, conforme. **Mais** :
1. Le **séparateur `0x00`** est unique → si `Label` ou `Context` contient un `0x00`, ambiguïté.
2. **Pas de version de schéma** : `Label = "session_key"` sans `v1` suffixe → toute rotation de structure (ajout de champ) crée une collision potentielle avec des clés antérieures.
3. **Pas de vérification de longueur** : `Label` ou `Context` de taille arbitraire acceptée.
4. **Variantes** : deux call sites utilisent le même `Label = "HESIA_KEY"` avec `Context` différents → risque de collision si `Context` lui-même est mal séparé.

Exemple concret :
- Call A : `KDF(k, "session", device_id || "_drone")`
- Call B : `KDF(k, "session_drone", device_id)`

Si la concaténation n'est pas rigoureusement séparée, A et B peuvent produire la même clé.

## Impact
- **Collision sémantique** : deux contextes différents produisent la même clé.
- **Cross-usage key leak** : clé issue du Label A utilisée comme clé B si l'attaquant contrôle une partie du Context.
- **Non-conformité NIST stricte** (SP 800-108 recommande sans imposer une séparation plus robuste via TLV).

## Scénario d'exploitation
Scénario théorique :
1. Attaquant qui contrôle `device_id` (via Faille_02 bootstrap TOFU) peut choisir un `device_id` tel que la concaténation collide avec une autre dérivation.
2. Exploite pour forcer une dérivation de clé identique entre deux contextes qu'il contrôle.

## Correctif recommandé
1. **TLV structuré** pour Label/Context :
   ```c
   struct kdf_input {
       uint32_t schema_version; // ex: 0x00010000
       uint16_t label_len;
       uint8_t  label[...];
       uint16_t context_len;
       uint8_t  context[...];
       uint32_t L; // bits output
   };
   ```
2. **Domain separator** obligatoire : Label commence toujours par `"HESIA|"`.
3. **Rejeter** Label ou Context contenant des octets `0x00` ambiguants.
4. **Limiter les tailles** : Label ≤ 64 octets, Context ≤ 256 octets.
5. **Journaliser** toutes les dérivations avec leur `(Label, Context)` pour audit.

## Dépendances
- Faille_05 : HKDF maison à remplacer aussi, structure TLV commune recommandée.

## Jetson requis
Non.

## Effort estimé
- 3 à 4 jours dev + 2 jours tests KAT cross-call.
