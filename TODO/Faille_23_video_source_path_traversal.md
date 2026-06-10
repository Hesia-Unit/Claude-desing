# Faille 23 — Path traversal potentiel sur source vidéo / captures

## Priorité : **P1 — Haute** · Gravité : ~6.3

## Localisation
- `drone_source/video_channel.cpp` :
  - Ouverture fichier source pour lecture (mode test/emulation)
- `drone_source/clean_pipeline.cpp` :
  - Sortie de frames vers un dossier configurable
- `drone_source/hesia_drone.cpp` :
  - Parse de config `video_source_path`
- `server_source/tools/ui_server.py` :
  - `/api/frame/<id>` construit un chemin depuis l'id fourni

## Description
Plusieurs chemins de fichier sont construits à partir de données **contrôlées par l'opérateur (config) ou par le réseau (drone_id, frame_id)** sans canonicalisation ni validation :

```cpp
// exemple simplifié
std::string path = video_base_dir + "/" + drone_id + "_" + frame_id + ".jpg";
std::ofstream out(path);
```

Si `drone_id = "../../../etc/passwd"` ou `frame_id = "../../.ssh/authorized_keys"`, le fichier est écrit en dehors du dossier autorisé.

Côté `ui_server.py` :
```python
frame_id = request.args.get("id")
path = os.path.join(BASE_DIR, frame_id + ".jpg")
return send_file(path)
```
→ si `frame_id = "../../../etc/shadow"`, on lit /etc/shadow.

## Impact
- **Lecture arbitraire** de fichiers serveur (via ui_server).
- **Écriture arbitraire** dans le FS (via video_channel en mode buffer on-disk).
- **Écrasement de fichiers système** si le process a les droits (par chance, la plupart des daemons tournent non-root via systemd hardening, mais le user `hesia` peut écrire dans `/var/lib/hesia` au minimum → accès à des clés ou config si path traversal y mène).

## Scénario d'exploitation
```bash
# attaquant via API UI (si combiné avec Faille_22)
curl "http://127.0.0.1:8080/api/frame/..%2F..%2F..%2Fetc%2Fshadow"
# -> /etc/shadow lu si le process a permission
```

Ou via drone_id compromis (Faille_02 bootstrap TOFU) :
```cpp
drone_id = "../../tmp/attack"
// frame écrite en /tmp/attack_xxx.jpg
```

## Correctif recommandé
1. **Whitelist regex stricte** pour tout identifiant utilisé dans un path :
   ```cpp
   if (!std::regex_match(drone_id, std::regex("^[a-zA-Z0-9_-]{3,32}$"))) {
       throw std::invalid_argument("drone_id format");
   }
   ```
2. **Canonicalisation + préfixe assertion** :
   ```cpp
   fs::path p = fs::canonical(base / frame_id);
   if (p.string().find(base.string()) != 0) throw "traversal";
   ```
3. **`openat` + `O_NOFOLLOW` + `AT_BENEATH`** (Linux) pour interdire symlinks et remontées.
4. **Dossier restreint** : chroot ou namespace monté read-only sauf dossier autorisé.
5. **Python `ui_server`** : utiliser `pathlib.Path.resolve()` + check `is_relative_to(base)`.
6. **Tests de fuzzing** sur les parseurs de path.

## Dépendances
- Faille_19 : frames world-readable + path traversal = double exfiltration.
- Faille_22 : API non authentifiée expose l'endpoint.

## Jetson requis
Non.

## Effort estimé
- 3 à 5 jours dev + 2 jours fuzzing.
