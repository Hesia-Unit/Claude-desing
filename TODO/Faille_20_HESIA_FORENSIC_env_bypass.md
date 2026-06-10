# Faille 20 — Variable d'environnement `HESIA_FORENSIC` contourne protections

## Priorité : **P1 — Haute** · Gravité : ~7.5

## Localisation
- `drone_source/hesia_drone.cpp` : checks `getenv("HESIA_FORENSIC")` à plusieurs endroits
- `drone_source/drone_network.cpp` : variables env relatives au niveau de log / écriture disque
- `server_source/src/main.cpp` : check similaire côté serveur
- Systemd units : `drone_transition_source/systemd/hesia-drone.service` et équivalent server

## Description
Le code comporte des **portes de diagnostic** activées par variables d'environnement :
- `HESIA_FORENSIC=1` : désactive certaines fonctions de sanitisation mémoire, dump l'état interne, loggue des clés partielles, allonge les timeouts.
- `HESIA_DEBUG_CRYPTO=1` : loggue les IV, AAD, et (partiellement) les plaintexts.
- `HESIA_SKIP_POLICY=1` : bypass de la policy signée (si présente).

Problèmes :
1. **Aucune vérification** que le binaire tourne en mode release : ces env vars sont lues systématiquement.
2. **Pas de gating par signature** : un opérateur malveillant ou un attaquant qui contrôle l'environnement systemd peut activer.
3. **Systemd override** : `systemctl edit hesia-drone.service` permet à root d'injecter `Environment="HESIA_FORENSIC=1"`.
4. **Fuite cross-process** : un autre service avec `PassEnvironment=HESIA_*` propage accidentellement.

## Impact
- **Leak massif** de secrets via logs : clés, IV, plaintexts partiels.
- **Bypass policy** : `HESIA_SKIP_POLICY=1` peut désactiver mTLS, bloquer la vérification measured boot.
- **Reverse engineering facilité** : l'attaquant voit les étapes internes.
- **Forensic false-positive** : un opérateur honnête peut activer pour debug et oublier.

## Scénario d'exploitation
```bash
# attaquant root sur drone ou serveur
systemctl edit hesia-drone.service
# ajouter : Environment="HESIA_FORENSIC=1" Environment="HESIA_DEBUG_CRYPTO=1"
systemctl restart hesia-drone
# logs journalctl -u hesia-drone : clés et IV exposés
journalctl -u hesia-drone | grep -i "session_key"
```

## Correctif recommandé
1. **Supprimer complètement** ces env vars du binaire release via `#ifdef HESIA_DEBUG` :
   ```cpp
   #ifdef HESIA_DEBUG
   bool forensic_mode = getenv("HESIA_FORENSIC") != nullptr;
   #else
   constexpr bool forensic_mode = false;
   #endif
   ```
2. **Build release** : flag `-DHESIA_PROD=1` retire tout path forensic.
3. **Assert au démarrage** : si build "prod" + env var détectée → `std::terminate` après log WARN.
4. **Audit des units systemd** : `systemd-analyze verify` pour vérifier absence de `Environment=HESIA_*` en production.
5. **Fingerprint du binaire release** : inclure la variante de build dans le message de `startup.log` (prod/debug) pour détection post-mortem.

## Dépendances
- Faille_03 : path cleartext relié, même philosophie.
- Faille_23 : path traversal + forensic = fuite disque.

## Jetson requis
Non (analyse statique + test sur build release).

## Effort estimé
- 3 à 5 jours dev + audit build cross-config.
