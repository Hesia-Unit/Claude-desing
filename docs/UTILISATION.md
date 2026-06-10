# UTILISATION

Ce document donne les commandes minimales pour :

- lancer le serveur HESIA en local sur cette machine
- lancer l'UI locale
- démarrer le drone sur `ajax`

Les commandes ci-dessous sont prévues pour `PowerShell` sous Windows.

## Prérequis

- la clé SSH `ajax-desktop` doit être opérationnelle
- `WSL` avec la distribution `Ubuntu` doit être installée
- `Tailscale` doit être actif sur cette machine

Test rapide :

```powershell
ssh ajax-desktop "hostname"
```

## 1. Lancer le serveur local

Cette commande démarre le serveur dans `WSL` :

```powershell
wsl -u root -d Ubuntu -- bash /mnt/c/Users/matis/Documents/Hesia-Firmware/tools/start_local_server.sh
```

Vérification :

```powershell
wsl -u root -d Ubuntu -- bash -lc 'pgrep -af hesia_server_cpp; ss -ltnp | grep ":9000 "; tail -n 40 /var/log/hesia/local-server-console.log'
```

## 2. Exposer le serveur local au drone via Tailscale

Cette commande publie le port `9000` local vers le réseau Tailscale :

```powershell
tailscale serve --bg --tcp 9000 127.0.0.1:9000
```

Vérification :

```powershell
tailscale serve status
```

## 3. Lancer l'UI locale

Cette commande démarre l'interface web locale sur `127.0.0.1:8080` :

```powershell
wsl -u root -d Ubuntu -- bash /mnt/c/Users/matis/Documents/Hesia-Firmware/tools/start_local_ui.sh
```

Vérification :

```powershell
wsl -u root -d Ubuntu -- bash -lc 'pgrep -af ui_server.py; ss -ltnp | grep ":8080 "; tail -n 40 /var/log/hesia/ui/ui-server.out'
```

Ouvrir ensuite :

```text
http://127.0.0.1:8080
```

Si l'interface affiche encore une ancienne version, faire un rechargement complet :

```text
Ctrl+F5
```

## 4. Arrêter le serveur distant sur ajax

Le drone doit se connecter au serveur local, donc le serveur distant sur `ajax` doit être arrêté :

```powershell
ssh ajax-desktop "echo '2008200419761954.' | sudo -S systemctl stop hesia-server.service"
```

## 5. Démarrer le drone sur ajax

Cette commande redémarre le drone :

```powershell
ssh ajax-desktop "echo '2008200419761954.' | sudo -S systemctl restart hesia-drone.service"
```

Vérification :

```powershell
ssh ajax-desktop "systemctl status hesia-drone.service --no-pager"
```

## 6. Arrêter le drone proprement

Pour arrêter le drone sans couper brutalement le process :

```powershell
ssh ajax-desktop "echo '2008200419761954.' | sudo -S systemctl stop hesia-drone.service"
```

## 7. Consulter les logs

Logs du drone sur `ajax` :

```powershell
ssh ajax-desktop "journalctl -u hesia-drone.service -f"
```

Logs du serveur local :

```powershell
wsl -u root -d Ubuntu -- bash -lc 'tail -f /var/log/hesia/local-server-console.log'
```

Logs de l'UI locale :

```powershell
wsl -u root -d Ubuntu -- bash -lc 'tail -f /var/log/hesia/ui/ui-server.out'
```

## 8. Séquence complète recommandée

Ordre d’exécution recommandé :

1. lancer le serveur local
2. exposer le port `9000` via `tailscale serve`
3. lancer l’UI locale
4. arrêter le serveur distant sur `ajax`
5. redémarrer le drone sur `ajax`
6. vérifier les logs serveur et drone

## 9. Commandes prêtes à copier

Serveur local :

```powershell
wsl -u root -d Ubuntu -- bash /mnt/c/Users/matis/Documents/Hesia-Firmware/tools/start_local_server.sh
```

Exposition Tailscale :

```powershell
tailscale serve --bg --tcp 9000 127.0.0.1:9000
```

UI locale :

```powershell
wsl -u root -d Ubuntu -- bash /mnt/c/Users/matis/Documents/Hesia-Firmware/tools/start_local_ui.sh
```

Arrêt serveur distant :

```powershell
ssh ajax-desktop "echo '2008200419761954.' | sudo -S systemctl stop hesia-server.service"
```

Démarrage drone :

```powershell
ssh ajax-desktop "echo '2008200419761954.' | sudo -S systemctl restart hesia-drone.service"
```
