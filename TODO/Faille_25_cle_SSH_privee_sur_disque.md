# Faille 25 — Clé SSH privée `hesia_jetson` présente sur disque

## Priorité : **P1 — Haute** · Gravité : ~8.0 (opérationnelle)

## Localisation
- `key-ssh/hesia_jetson` (clé privée OpenSSH, sur disque dev)
- `key-ssh/hesia_jetson.pub` (clé publique correspondante, trackable)
- `.gitignore` : contient `key-ssh/` → **non committé** (bon point), mais le fichier existe physiquement
- `tools/jetson_ssh.sh:ligne ~12` : chemin hardcodé `/home/valstrax/.ssh/hesia_jetson`

## Description
Lors de l'audit, une clé SSH privée a été trouvée sur le poste dev à `C:\Users\matis\Documents\Hesia-Firmware\key-ssh\hesia_jetson`. Cette clé donne accès SSH au(x) Jetson(s) du projet.

Observations :
- La clé n'est pas dans git (exclue par `.gitignore`), bon point.
- Mais elle est **physiquement sur le disque du poste dev**, potentiellement :
  - Sans passphrase (à vérifier sur le fichier).
  - Sauvegardée par des outils automatiques (cloud sync, backup Windows).
  - Copiable par tout malware sur la machine.
- Le script `tools/jetson_ssh.sh` code en dur `/home/valstrax/.ssh/hesia_jetson` → révèle que l'identité dev s'appelle `valstrax`, utile pour un attaquant ciblant la personne.
- Pas de mécanisme de rotation / révocation documenté.

## Impact
- **Compromission poste dev = accès SSH Jetson** : tout attaquant ayant un RCE sur le PC dev peut se connecter aux Jetson de prod/staging.
- **Persistance** : la clé peut être copiée sans détection, remontée ailleurs.
- **Pas de traçabilité** : plusieurs devs peuvent utiliser la même clé, attribution impossible en cas d'incident.
- **Identité dev leakée** : `/home/valstrax/` exposé dans les scripts shell trackés.

## Scénario d'exploitation
1. Dev clique sur un phishing → malware scanne `%USERPROFILE%\Documents\` et `%USERPROFILE%\.ssh\`.
2. Clé SSH exfiltrée.
3. Attaquant scanne le réseau / rebond via VPN → atteint le Jetson.
4. Accès root si la clé est mappée root, sinon escalade via CVE locale.

## Correctif recommandé
1. **Retirer la clé du dépôt de travail** : la stocker dans un vault (1Password, Bitwarden, HashiCorp Vault) et la matérialiser à la demande.
2. **Passphrase obligatoire** sur toute clé privée dev.
3. **Rotation trimestrielle** : `ssh-keygen -t ed25519` régulier + révocation côté Jetson.
4. **Clés par développeur** : `hesia_jetson_<username>` chacun avec sa propre clé, allowlist côté `authorized_keys`.
5. **Bastion** : passer par un bastion avec audit/enregistrement de session plutôt que SSH direct.
6. **Hardware token** : YubiKey SSH (FIDO2) pour les accès prod.
7. **Script `jetson_ssh.sh`** : lire le chemin depuis `$HESIA_JETSON_KEY` env, par défaut `~/.ssh/hesia_jetson`, **pas hardcodé `/home/valstrax/`**.
8. **Audit** : `find` sur le poste dev pour détecter toute copie accidentelle.
9. **Supprimer `/home/valstrax/`** des fichiers versionnés (grep complet pour trouver toutes les occurrences).

## Dépendances
- Faille_21 : scripts shell → rotation des clés SSH doit être scriptée proprement.
- Faille_18 : allowlist signée.

## Jetson requis
Non pour la correction (organisationnel). Oui pour valider `authorized_keys` post-rotation.

## Effort estimé
- 1 jour technique + 1 semaine organisationnelle (vault, process, formation devs).
