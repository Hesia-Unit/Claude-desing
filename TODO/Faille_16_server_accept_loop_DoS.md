# Faille 16 — Serveur : accept loop vulnérable à slowloris / DoS

## Priorité : **P1 — Haute** · Gravité : ~6.8

## Localisation
- `server_source/src/main.cpp` : boucle principale `::accept` / `tls_accept`
- `server_source/src/hesia_server_session.cpp` :
  - `read_frame` (bloquant, sans timeout strict)
  - `handshake` (plusieurs round-trips sans guard timer)

## Description
La boucle d'accept du serveur HESIA :
1. Accepte une socket TCP.
2. Crée une `HesiaServerSession` dans un thread détaché (ou pool).
3. Invoque `tls->accept` puis `session.run_handshake` sans limite de temps stricte.

Observations :
- Aucun `SO_RCVTIMEO` / `SO_SNDTIMEO` explicite sur les sockets.
- Aucun `accept4(SOCK_NONBLOCK)`, donc le handshake peut bloquer indéfiniment.
- Pas de limite globale sur le nombre de handshakes en cours (pas de semaphore).
- Pas de quota par IP source.
- La session handshake tire plusieurs PQC operations (ML-KEM, ML-DSA) coûteuses → un attaquant qui ouvre N sessions partielles consomme N × ~MB de RAM et du CPU.

## Impact
- **Slowloris TLS** : attaquant ouvre 10k connexions, envoie 1 octet toutes les 10s → épuise les threads du pool / sockets FD.
- **Amplification CPU** : chaque handshake tentatif tire ML-KEM keygen → ~2 ms CPU side serveur, ~1 Mbps d'attack suffit à monopoliser un core.
- **OOM** : session state ~MB, 10k sessions = ~GB RAM.
- **Régression opérationnelle** : drones légitimes ne peuvent plus se connecter.

## Scénario d'exploitation
```python
# attaquant DoS
for _ in range(10000):
    s = socket.socket()
    s.connect((target, 8443))
    s.sendall(b"\x16\x03\x03\x00\x01")  # TLS partial
    # ne rien faire de plus, garder ouvert
```

## Correctif recommandé
1. **Timeouts agressifs** :
   - `setsockopt(SO_RCVTIMEO, 5s)` sur chaque socket accept.
   - Deadline handshake 10s maximum (timer dédié).
2. **Limite globale** : `std::counting_semaphore<MAX_CONCURRENT_HANDSHAKES>`, ex. 256.
3. **Quotas par IP** : token bucket (ex. 5 handshakes/min par /24).
4. **connlimit firewall** : `iptables -A INPUT -p tcp --dport 8443 --syn -m connlimit --connlimit-above 10 -j DROP`.
5. **Pré-filtre SYN-cookie** pour le premier niveau.
6. **Fail2ban** sur motifs de handshake échoués répétés.
7. **Logs structured** : tracer chaque accept/handshake pour dashboarding.

## Dépendances
- Faille_22 : API UI serveur peut aussi être abusée.
- Faille_09 : reflèt la philosophie générale "pas de bornes dures".

## Jetson requis
Non (serveur Linux standard, testable sur VM).

## Effort estimé
- 1 semaine dev + 3 jours tests de charge.
