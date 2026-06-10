# Faille 19 — Frames vidéo serveur écrites world-readable sur disque

## Priorité : **P1 — Haute** · Gravité : ~6.0

## Localisation
- `server_source/src/hesia_server_session.cpp` :
  - `handle_video_frame` (écriture dans un dossier `captures/` ou `frames/`)
- `server_source/tools/ui_server.py` :
  - Serveur HTTP local qui expose les frames au UI browser
- `server_source/ui/app.js` : consomme via `/api/frame/<id>`

## Description
Les frames vidéo reçues par le serveur (déchiffrées par le `VideoChannel`) sont sauvegardées sur disque pour :
- Debugging / forensic.
- Affichage dans l'UI web locale.

Observations :
- Pas de chiffrement at-rest des frames.
- Pas de permissions restrictives explicites : `open(O_CREAT, 0644)` (world-readable).
- Rotation/purge non documentée → accumulation indéfinie.
- L'UI serveur (`ui_server.py`) sert ces fichiers via HTTP sur 127.0.0.1 → accessible à tout process local.

## Impact
- **Fuite de contenu sensible** : vidéo drone en clair sur disque accessible à tout user.
- **Forensic involontaire** : accumulation massive (GB/jour) sans politique de rétention.
- **Cross-tenant leak** : si plusieurs drones/opérateurs partagent le serveur, leurs frames se retrouvent au même endroit.

## Scénario d'exploitation
Un user non-privilégié sur le serveur :
```bash
ls -la /var/lib/hesia/captures/
# -rw-r--r--  hesia hesia  123456  2026-04-24 10:00  drone_001_frame_000042.jpg
cp /var/lib/hesia/captures/*.jpg /tmp/exfil/
```

## Correctif recommandé
1. **Permissions strictes** : `umask(0077)` + `chmod 0600` sur chaque fichier.
2. **Répertoire `0700 hesia:hesia`**.
3. **Chiffrement at-rest** : chaque frame chiffrée par AES-256-GCM avec une clé dérivée d'un master key stocké dans le TPM ou vault.
4. **Rotation automatique** : suppression sécurisée (`shred`) après X heures ou X Go.
5. **UI serveur** : authentification par token (session) + HTTPS local avec cert auto-signé pinné, pas de listing de répertoire.
6. **Option de non-persistence** : par défaut, ne rien écrire sur disque, stream direct vers l'UI via WebSocket chiffré.
7. **Politique de rétention** documentée et appliquée via systemd-tmpfiles ou cron.

## Dépendances
- Faille_22 : UI non authentifiée → accès HTTP direct.
- Faille_24 : secrets non zéroisés aggrave si frames contiennent métadonnées clé.

## Jetson requis
Non.

## Effort estimé
- 3 à 5 jours dev + 2 jours tests end-to-end + 1 jour doc politique rétention.
