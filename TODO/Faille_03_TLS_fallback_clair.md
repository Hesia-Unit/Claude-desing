# Faille 03 — Chemin transport en clair (non-TLS) toujours compilable

## Priorité : **P0 — Critique** · Gravité : ~8.8 (Haute / Critique)

## Localisation
- `drone_source/drone_network.hpp:75-82` (champs mutables `tls_enabled`, `tls_verify_peer`, `tls_pin_server_pubkey`)
- `drone_source/drone_network.cpp:594-598` (initialisation sous garde policy mais re-assignable)
- `drone_source/drone_network.cpp:632-648` (fallback `::send`/`::recv` en clair)
- `drone_source/drone_network.cpp:659-661` (lecture fallback)
- `drone_source/hesia_drone.cpp:1509` (salt de binding mis à 32 zéros si `use_tls_exporter_binding=false`)

## Description
Les champs `tls_enabled`, `tls_verify_peer`, `tls_pin_server_pubkey` sont des `bool` mutables initialisés à `true` par défaut puis réaffectés dans le constructeur sous la garde `policy_.require_mtls`. `transport_write_all()` et `transport_read_all()` possèdent une **branche fallback** qui envoie/reçoit en clair via `::send`/`::recv` si `!tls_enabled || !tls->is_connected()`.

La branche `else` à `drone_network.cpp:594-598` **efface explicitement** l'exporter TLS et `tls_peer_cert_sha256`, matérialisant un chemin "pas de TLS" dans le binaire de production.

Bien que la policy signée protège la production, trois vecteurs restent :
1. Une régression de build / un flag dev mal désactivé active le chemin clair.
2. Un attaquant capable d'injection mémoire (fault injection, Rowhammer, cf. modules existants `fault_injection_protection.cpp`) peut flipper le bit `tls_enabled`.
3. Le binding `HKDF(session_key, "TLS-EXPORTER", tls_exporter_secret)` devient `HKDF(session_key, "TLS-EXPORTER", 0x00...00)` silencieusement.

## Impact
- **Canal drone↔serveur entièrement passable en clair** sans autre garantie que la signature ML-DSA du handshake et le chiffrement GCM applicatif (dont les clés dérivent du ML-KEM — mais sans binding transcript TLS).
- **MITM possible** sur la signalisation (`VIDEO_DATA` prefix, `uint32_t length`, type tags `0x02`).
- **Confidentialité vidéo compromise** : `VideoChannel` chiffre la payload GCM, mais les 24 octets d'en-tête (stream_id, frame_id, IV) ne sont couverts QUE par TLS.
- **Downgrade attack** : un attaquant qui contrôle le socket peut forcer `tls->is_connected() = false` et bypasse.

## Scénario d'exploitation
Configuration dev laissée en prod : `policy_.require_mtls=0` → `tls_enabled=0` → serveur hostile sur même LAN → MITM transparent.

Ou plus subtil : un glitching de tension (Faille par Flash voltage drop, cf. `voltage_glitch_protection.cpp`) cible le load du flag `tls_enabled` → instantané de downgrade.

## Correctif recommandé
1. **Supprimer la branche no-TLS** du binaire production via flag de compilation :
   ```cpp
   #ifdef HESIA_PROD
   static_assert(true, "TLS mandatory");
   if (!tls_enabled) { std::terminate(); }
   #endif
   ```
2. **Rendre `tls_enabled` const** calculé à la construction :
   ```cpp
   const bool tls_enabled;
   DroneNetworkClient() : tls_enabled(policy_.require_mtls) { ... }
   ```
3. **Refuser le démarrage** si `use_tls_exporter_binding=false` en prod, via assertion dure dans `HesiaDrone::handle_key_init`.
4. **Retirer** le fallback `::send`/`::recv` du `transport_*` : forcer toute I/O à passer par `tls->write/read`, sans chemin alternatif.
5. Chiffrement AEAD applicatif devrait **toujours** inclure l'exporter TLS dans son AAD — aujourd'hui le binding TLS n'est utilisé que dans le handshake ML-KEM.

## Dépendances
- Faille_07 (AAD sender/receiver) : si TLS tombe, le seul rempart restant est l'AAD GCM.
- Faille_06 (TA sealing sans AAD) : même chaîne d'erreurs.

## Jetson requis
Non, analyse statique suffit. Vérification dynamique sur Jetson recommandée pour confirmer qu'un build release ne laisse pas passer `tls_enabled=0`.

## Effort estimé
- 3 à 5 jours dev + 1 semaine de validation cross-build.
