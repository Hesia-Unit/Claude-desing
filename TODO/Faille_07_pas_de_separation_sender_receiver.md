# Faille 07 — Absence de séparation de clés sender/receiver sur SecureChannel

## Priorité : **P1 — Haute** · Gravité : ~7.2

## Localisation
- `drone_source/secure_channel.cpp:195-228` (constructeur avec `session_key` unique)
- `drone_source/secure_channel.cpp:280-330` (`encrypt`/`decrypt` utilisent la même clé dans les deux sens)
- `drone_source/secure_channel.cpp:60-74` (`build_iv` partage également les préfixes)
- `server_source/src/hesia_server_session.cpp` (côté symétrique, même clé utilisée par le serveur)

## Description
Le `SecureChannel` AES-256-GCM utilise **une unique `session_key`** pour chiffrer le flux drone→serveur ET serveur→drone. Il n'y a pas de dérivation séparée d'une `key_drone_to_server` et d'une `key_server_to_drone` par HKDF à partir du secret partagé ML-KEM.

Le `iv_prefix` (32 bits random) est indépendant côté drone et côté serveur, ce qui réduit (mais n'élimine pas) la probabilité de collision croisée.

Cette construction est non conforme aux bonnes pratiques TLS/QUIC : même dans TLS 1.3, des `client_application_traffic_secret` et `server_application_traffic_secret` distincts sont dérivés de `master_secret`.

## Impact
- **Oracle de déchiffrement reflété** : si le serveur ne valide pas correctement l'AAD (direction, role, sequence), un attaquant peut capturer un paquet serveur→drone et le rejouer au serveur en tant que drone→serveur avec réutilisation de `(IV, key)`.
- **Combinée à Faille_04** (nonce reuse) : la probabilité de collision IV est doublée puisque les deux parties piochent dans le même espace d'IV pour la même clé.
- **Confusion sémantique** : un message destiné à être reçu par le drone (ex : commande) peut, si collision IV, révéler par XOR un plaintext envoyé par le drone (télémétrie).

## Scénario d'exploitation
1. Attaquant MITM capture un message M1 de S→D avec IV1.
2. Attente d'un message M2 de D→S avec le même IV1 (probabilité ~2^-32 par paire, mais agrégée sur plusieurs clusters).
3. Si AAD ne distingue pas les sens, `XOR(C1, C2) = XOR(P1, P2)` → plaintexts récupérables.

## Correctif recommandé
1. **Dériver deux clés distinctes** à partir de la clé de session :
   ```cpp
   std::array<uint8_t, 32> k_d2s = HKDF(shared, "HESIA_D2S_v1", "");
   std::array<uint8_t, 32> k_s2d = HKDF(shared, "HESIA_S2D_v1", "");
   ```
2. **Ajouter un octet `role`** (`0x01` = drone, `0x02` = serveur) dans l'AAD de chaque paquet GCM.
3. **Séparer les `send_counter` et `recv_counter`** en deux atomic distincts.
4. **Imposer `key_drone_to_server != key_server_to_drone`** via assertion au démarrage.
5. Cette correction doit intégrer Faille_05 (HKDF propre).

## Dépendances
- Faille_04 : correction nonce doit inclure le split sender/receiver.
- Faille_05 : HKDF propre nécessaire pour dériver les deux clés.

## Jetson requis
Non (faille d'implémentation crypto).

## Effort estimé
- 4 à 6 jours dev + 3 jours tests (KAT direction, cross-reflection).
