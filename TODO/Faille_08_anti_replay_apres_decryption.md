# Faille 08 — Vérification anti-rejeu effectuée après déchiffrement GCM

## Priorité : **P1 — Haute** · Gravité : ~6.8

## Localisation
- `drone_source/secure_channel.cpp` :
  - `decrypt()` (≈ lignes 320-400)
  - Check `sequence` et `replay_window` appelés après `EVP_DecryptFinal_ex`
- `server_source/src/hesia_server_session.cpp` : symétrique (sliding window)

## Description
Le `SecureChannel::decrypt()` suit ce flow :
1. Parse du header (IV, tag, length)
2. `EVP_DecryptInit` + `EVP_DecryptUpdate` + `EVP_DecryptFinal_ex`
3. Si OK → extraction du `sequence_number` depuis le plaintext → check dans `replay_window`
4. Si séquence déjà vue → rejet

**Problème** : la vérification d'anti-rejeu est appliquée **après** que GCM ait déjà mutualisé la clé/IV avec le payload. Un attaquant qui rejoue un paquet déclenche le full déchiffrement, ce qui est :
- Un **gaspillage CPU** exploitable en DoS (amplification : un paquet rejoué = 1 déchiffrement AES-GCM complet).
- Un **side channel potentiel** : le temps de traitement entre "IV invalide" et "rejeu détecté" diffère, l'attaquant peut profiler les séquences de compteur interne.

De plus, il n'y a pas de pre-check sur l'IV/compteur avant le GCM : tout paquet avec un format correct sera déchiffré.

## Impact
- **Amplification DoS** : un attaquant capturant 10 paquets légitimes peut les rejouer en boucle à ~1M/s, chaque replay force un GCM complet côté drone et serveur.
- **Side-channel temporel** : différentiel entre "pre-replay-window" et "post-replay-window" leak le compteur interne.
- **Pré-check manquant** : aucun filtre rapide par `iv_prefix` connu.

## Scénario d'exploitation
```python
# attaquant DoS
legit_packets = capture_sniff(1000)
while True:
    for p in legit_packets:
        send(target, p)  # 10k pps trivial
# Côté cible : full AES-GCM à chaque paquet, CPU drainé, video dégradée
```

## Correctif recommandé
1. **Inverser l'ordre** : parser le numéro de séquence depuis l'**AAD** (donc non chiffré mais authentifié), vérifier la fenêtre anti-rejeu, puis **déchiffrer seulement si la séquence est neuve**.
   ```cpp
   uint64_t seq = read_be64(aad + OFFSET_SEQ);
   if (!replay_window_check(seq)) {
       return Error::Replay; // avant GCM
   }
   // puis GCM
   ```
2. **Ajouter un token HMAC-SHA256 rapide** sur (iv_prefix || seq || key_epoch) en pré-filtre, pour rejeter les paquets malformés sans GCM.
3. **Rate-limit par source IP** et par drone_id (déjà partiellement dans `server_hesia_session.cpp` mais à durcir).
4. **Mesurer** le coût de rejet constant pour éviter side-channel.

## Dépendances
- Faille_04 (nonce IV) : lié car le pre-check doit se faire sur l'IV.
- Faille_07 (sender/receiver split) : la fenêtre anti-rejeu doit être par direction.

## Jetson requis
Non (mais test de soak DoS sur Jetson recommandé pour mesurer impact réel).

## Effort estimé
- 3 à 5 jours dev + 2 jours tests DoS synthétiques.
