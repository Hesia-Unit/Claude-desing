# Faille 05 — KDF HKDF-HMAC-SHA3-512 maison non conforme RFC 5869

## Priorité : **P0 — Critique** · Gravité : ~7.8

## Localisation
- `drone_source/security_utils.cpp:575-604` (`HKDF::extract/expand/derive`)
- `drone_source/security_utils.cpp:268-304` (`hmac_sha3_512_bytes`)
- `drone_source/secure_channel.cpp:454-478` (`rotate_key` utilise `HKDF::derive` maison)
- `drone_source/kdf_sp800_108.cpp` (KDF SP 800-108 validé **non utilisé** pour la rotation)

## Description
L'implémentation C++ `HKDF::derive()` est une construction maison sur HMAC-SHA3-512 :
1. **HMAC-SHA3-512 n'est pas FIPS-approved pour les usages HKDF** : FIPS 180-4 et FIPS 198-1 définissent HMAC-SHA3 mais les évaluations NIST standard ne couvrent pas cette composition en tant que HKDF (RFC 5869 est défini sur SHA-256/384/512, pas SHA3-*). Un module FIPS doit passer par HKDF-SHA256 via `EVP_KDF_fetch("HKDF")`.
2. **Compteur `uint8_t`** : boucle jusqu'à 255 → limite dur L = 255 × 64 = 16 320 octets. Actuellement seuls 32 octets sont dérivés, mais aucune assertion ne verrouille cette limite.
3. **Domaines mélangés** : le projet dispose pourtant de `kdf_sp800_108_hmac_sha256()` validable, mais `rotate_key()` utilise la variante maison → incohérence de politique crypto.
4. **Hash fonction non-standard pour HKDF** : aucune revue cryptographique publique n'a validé HKDF-HMAC-SHA3-512 avec la structure exacte implémentée.

## Impact
- **Non-conformité FIPS** dès activation du chemin. Le module sort de l'Approved Mode.
- **Surface d'audit invisible** : toute fuite/biais dans l'implémentation maison passe le filet FIPS Self-Tests existants (qui ne couvrent pas cette KDF).
- **Propagation de faiblesse** sur toutes les clés dérivées :
  - `session_key` rotatée
  - `key_integrity_hmac_key`
  - `video_key = HKDF(session_key, "HESIA_VIDEO_STREAM_v1")`
- Risque de **cross-domain collision** si deux usages différents utilisent la même `info` string.

## Scénario d'exploitation
Aucune attaque publique directe connue sur HKDF-HMAC-SHA3-512, mais :
1. Le certificateur tiers (FIPS / CC) **rejettera** le module.
2. Une régression d'implémentation (copie/collage modifié) peut introduire une collision non détectée.
3. Les évaluateurs red team auront un axe d'attaque privilégié (le code maison est plus facile à tromper que OpenSSL).

## Correctif recommandé
1. **Supprimer** `HKDF::derive` / `HKDF::extract` / `HKDF::expand` maison.
2. **Remplacer par `EVP_KDF_fetch("HKDF")`** d'OpenSSL avec SHA-256 (standard, rapide, validé) :
   ```cpp
   EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
   EVP_KDF_CTX *ctx = EVP_KDF_CTX_new(kdf);
   OSSL_PARAM params[] = {
       OSSL_PARAM_utf8_string("digest", "SHA256", 0),
       OSSL_PARAM_octet_string("salt", salt.data(), salt.size()),
       OSSL_PARAM_octet_string("key", ikm.data(), ikm.size()),
       OSSL_PARAM_octet_string("info", info.data(), info.size()),
       OSSL_PARAM_END
   };
   EVP_KDF_derive(ctx, out.data(), out.size(), params);
   ```
3. **Rediriger** tous les call sites (`secure_channel.cpp:454`, `video_channel.cpp`, etc.) vers cette API.
4. **Assertion** : `out_len <= 255 * 32` (pour SHA-256).
5. **Déprécier `hmac_sha3_512_bytes`** si aucun autre usage critique ne le demande.

## Dépendances
- Faille_04 : l'Option C du correctif dépend d'une KDF propre.
- Faille_17 : `kdf_sp800_108` a aussi un défaut de séparateur, à corriger en parallèle.

## Jetson requis
Non (implémentation crypto, analyse statique).

## Effort estimé
- 1 semaine dev + 3 jours tests (KAT, régression).
