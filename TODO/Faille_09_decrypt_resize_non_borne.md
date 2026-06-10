# Faille 09 — `decrypt()` resize de buffer sans borne d'intégrité

## Priorité : **P1 — Haute** · Gravité : ~6.5

## Localisation
- `drone_source/secure_channel.cpp` :
  - `decrypt()` : `plain.resize(ciphertext.size())` avant GCM (≈ ligne 335)
- `drone_source/drone_network.cpp` :
  - `transport_read_all()` alloue `length` octets depuis un `uint32_t` wire (≈ ligne 650)
- `server_source/src/hesia_server_session.cpp` : idem côté serveur

## Description
Le flow de réception est :
1. Lecture de 4 octets wire : `uint32_t length = be32toh(...)`.
2. Allocation de `std::vector<uint8_t> ciphertext(length)`.
3. `recv` de `length` octets.
4. `plain.resize(length)` puis GCM.

**Aucune borne absolue** n'est appliquée sur `length` avant l'allocation. Les seules garde-fous trouvés :
- Contrôle soft `length < MAX_FRAME` dans `drone_network.cpp` mais la constante n'est pas documentée et vaut 16 MiB.
- Pas de pré-check de cohérence entre `length` et les types de message attendus (HELLO, KEY_INIT, etc.).

## Impact
- **OOM DoS distant** : un pair TLS malveillant (ou MITM sur Faille_03) envoie `length = 0xFFFFFFFF` → allocation 4 GiB tentée → `std::bad_alloc` ou swap → crash process drone/serveur.
- **Fragmentation mémoire** : envoi répété de tailles 16 MiB fragmente le heap, dégrade perf long-running.
- **Pre-auth** : ce check est fait avant vérification GCM/AAD, donc avant que le pair soit authentifié cryptographiquement (dans certains cas post-handshake, mais bien avant authentification applicative côté serveur durant les premiers frames).

## Scénario d'exploitation
```python
# attaquant qui atteint le socket serveur (ou MITM drone)
attack_frame = struct.pack(">I", 0xFFFFFFFF) + b"\x00" * 100
send(target, attack_frame)
# serveur tente d'allouer 4 GiB avant même de valider le GCM
```

Ou plus subtil : envoyer `length = 100_000_000` répété en parallèle (100 connexions) → 10 GiB alloués → OOM killer.

## Correctif recommandé
1. **Borne dure par type de message** :
   ```cpp
   constexpr size_t MAX_HELLO = 4096;
   constexpr size_t MAX_KEY_INIT = 8192;
   constexpr size_t MAX_FRAME_VIDEO = 256 * 1024;
   constexpr size_t MAX_FRAME_CTRL = 64 * 1024;
   if (length > max_for_type(msg_type)) return Error::FrameTooLarge;
   ```
2. **Borne absolue** : rejeter toute `length > 1 MiB` avant allocation.
3. **Allocation paresseuse** : lire les données en stream dans un buffer pool de taille fixe.
4. **Rate-limit par connexion** : max 10 grosses frames/seconde.
5. **Cryptographic pre-auth** : le handshake devrait imposer un MAC rapide avant d'accepter des frames > 4 KiB.

## Dépendances
- Faille_03 (TLS fallback) : si TLS tombe, aucune protection transport.
- Faille_08 (replay) : lié à la structure de réception.

## Jetson requis
Non (fuzzing synthétique suffisant).

## Effort estimé
- 2 à 3 jours dev + 2 jours tests fuzzing frame (libFuzzer sur parseurs).
