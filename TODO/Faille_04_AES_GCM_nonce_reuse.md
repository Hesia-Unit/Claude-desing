# Faille 04 — Réutilisation potentielle de nonce AES-GCM (iv_prefix 32-bit)

## Priorité : **P0 — Critique** · Gravité : ~9.1

## Localisation
- `drone_source/secure_channel.cpp:93-112` (`regenerate_iv_prefix_or_throw`)
- `drone_source/secure_channel.cpp:195-228` (constructeur, `send_counter` init à 0)
- `drone_source/secure_channel.cpp:60-74` (`build_iv`)
- `drone_source/secure_channel.cpp:454-478` (`rotate_key`, remet compteur à 0)

## Description
L'IV GCM 96-bit est construit comme `[iv_prefix 32-bit] || [counter 64-bit BE]`. Le `iv_prefix` provient de `regenerate_iv_prefix_or_throw()` :
```cpp
static std::atomic<uint32_t> g_prefix_ctr{0}; // remis à 0 à chaque démarrage du process
RAND_bytes(rnd.data(), 4);                    // 32 bits d'entropie uniquement
iv_prefix ^= static_cast<uint8_t>((ctr >> N) & 0xFF);
```

Problèmes cumulés :
1. **32 bits seulement** de randomness dans le préfixe → espace réduit.
2. **Compteur process-local** : un redémarrage du drone repart à `g_prefix_ctr = 0`.
3. **`send_counter` remis à 0** dans le constructeur (ligne 202) et dans `rotate_key` (ligne 470).
4. `key_epoch` est certes dans l'AAD mais **pas dans l'IV**. Deux `SecureChannel` initialisés avec la même `session_key` (ex : cluster de drones, snapshot+restore firmware) ont `key_epoch=0` et risquent collisions de préfixe.

Probabilité de collision (paradoxe des anniversaires) : ~50% dès √(2·2^32) ≈ 93 000 sessions utilisant la même clé.

## Impact
En cas de collision de `(key, iv_prefix)` entre deux sessions utilisant la même `session_key` :
- **Deux messages** avec même IV, même clé, plaintexts différents → `P1 ⊕ P2 = C1 ⊕ C2` (récupération XOR des plaintexts).
- **Forgery GCM universel** possible via récupération de `H` (hash subkey) et calcul d'un tag arbitraire (attaque Joux / Handschuh-Preneel).

## Scénario d'exploitation
- Réutilisation de clé de session en cas de rollback (Faille_01) : le drone remonté à un snapshot antérieur ré-utilise la même `session_key` avec `iv_prefix` ré-aléatoirisé mais dans un espace 32-bit.
- Cluster de drones partageant une clé de groupe (architecture future potentielle) : explosion du nombre de sessions simultanées.
- Long-running session : `send_counter` 64-bit → 2^64 messages avant overflow, mais le préfixe seul détermine l'unicité cross-session.

## Correctif recommandé
### Option A — Compteur monotone non-volatile
- Persister un compteur 64-bit dans le TEE/fuse, incrémenté à chaque initialisation de `SecureChannel`.
- Préfixe IV = 8 octets = `boot_counter[8]`, compteur IV = 4 octets = message index.
- Maximum 2^32 messages par session, rotation obligatoire au-delà.

### Option B — IV 96-bit entièrement aléatoire
- `RAND_bytes(iv.data(), 12)` à chaque message.
- Borne stricte : 2^32 messages par clé (limite cryptographique pour IVs aléatoires).

### Option C — Dérivation HKDF déterministe
- `iv_prefix = HKDF(session_key, "hesia-ivprefix-v1", device_id || boot_counter)[0..4]`.
- Compteur message reste 64-bit BE.
- Chaque device a un préfixe unique fonction de son identité.

## Dépendances
- Faille_05 : HKDF maison non conforme, donc Option C risque de propager d'autres défauts si on l'utilise telle quelle. Corriger Faille_05 d'abord.
- Faille_07 : séparation sender/receiver → la correction doit intégrer un `role` dans la dérivation.

## Jetson requis
Non (faille d'implémentation crypto). Mais **test dynamique** recommandé : instrumenter pour loguer chaque IV et vérifier unicité sur une fenêtre de soak test.

## Effort estimé
- 1 à 2 semaines dev + 1 semaine test crypto.
