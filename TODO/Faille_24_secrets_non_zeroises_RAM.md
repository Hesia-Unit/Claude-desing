# Faille 24 — Secrets non zéroisés en RAM / `std::vector<uint8_t>` sans scrubbing

## Priorité : **P1 — Haute** · Gravité : ~5.8

## Localisation
- `drone_source/secure_channel.cpp` : `session_key`, `video_key`, `key_integrity_hmac_key` en `std::array<uint8_t, 32>` membres
- `drone_source/security_utils.cpp` : buffers temporaires HKDF/HMAC sans cleanse
- `drone_source/crypto_real.cpp` : `shared_secret` ML-KEM en `std::vector<uint8_t>`
- `drone_source/hesia_drone.cpp` : chargement de la session_auth en `std::string`
- `server_source/src/hesia_server_session.cpp` : idem côté serveur

## Description
Les secrets (clés AES, secrets de session, shared_secret ML-KEM, digests intermédiaires HMAC) vivent dans des conteneurs C++ standard :
- `std::array<uint8_t, N>` sur la stack : détruit par unwinding mais pas scrubé.
- `std::vector<uint8_t>` : `free()` sans memset → contenu reste sur le heap.
- `std::string` : idem, avec plus de risque de réallocation qui laisse une copie orpheline.

L'implémentation ne fait pas appel systématique à :
- `OPENSSL_cleanse(ptr, n)` (ou `explicit_bzero`)
- `secure_allocator<T>` pour les containers
- Lock mémoire (`mlock`) pour empêcher swap

Conséquences :
- Secrets persistent en RAM après usage.
- Un dump mémoire (via `/proc/<pid>/mem`, core dump, hibernation) révèle les clés.
- Swap disque écrit les clés si mémoire sous pression.

Note : le projet dispose d'une classe `SecureBytes` dans `security_utils.cpp` (observée par l'agent), mais l'adoption n'est **pas universelle**.

## Impact
- **Exfiltration post-compromission** : un attaquant avec lecture `/proc/<pid>/mem` (CAP_SYS_PTRACE ou user match) récupère les clés actuelles et anciennes.
- **Forensic hostile** : analyse post-mortem d'un drone saisi révèle l'historique des clés.
- **Swap leak** : si `/swap` n'est pas chiffré (cf. policy Jetson Orin Nano Super SD-boot), clés écrites.

## Scénario d'exploitation
```bash
# attaquant root REE sur drone
gdb -p $(pgrep hesia-drone)
(gdb) dump memory /tmp/dump.bin 0x... 0x...
# parse dump pour récupérer patterns de clés (entropie élevée sur 32 octets contigus)
```

## Correctif recommandé
1. **Centraliser sur `SecureBytes`** : tous les containers de secrets → `SecureBytes` avec destructeur qui `OPENSSL_cleanse`.
2. **`mlock` au démarrage** : locker les pages critiques avec `mlock2(..., MLOCK_ONFAULT)` pour empêcher swap.
3. **`madvise(..., MADV_DONTDUMP)`** sur régions critiques pour les exclure des core dumps.
4. **Désactiver core dumps** : `setrlimit(RLIMIT_CORE, 0)` + systemd `LimitCORE=0`.
5. **Swap chiffré** : systemd `CryptSwap` obligatoire dans la policy Jetson.
6. **Hibernation** : désactiver ou chiffrer (sinon clés persistent dans `resume`).
7. **Audit systématique** : script grep qui liste tous les `std::array<uint8_t` et `std::vector<uint8_t>` dans le crypto path, valider chacun.
8. **Tests leak** : ASan / Valgrind + inspection post-destruction.

## Dépendances
- Faille_11 : wipe partiel TA → similitude sur versant RAM.
- Faille_25 : clé SSH sur disque.

## Jetson requis
Oui pour test memory dump réel (sans CAP_SYS_PTRACE, pas de répro exploitable).

## Effort estimé
- 2 semaines dev (refactor containers + intégration mlock) + 1 semaine tests.
