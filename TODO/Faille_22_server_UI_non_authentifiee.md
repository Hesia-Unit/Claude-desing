# Faille 22 — API UI serveur non authentifiée sur localhost

## Priorité : **P1 — Haute** · Gravité : ~7.0

## Localisation
- `server_source/tools/ui_server.py` :
  - Serveur HTTP Python (Flask ou http.server) bind `127.0.0.1:<port>`
  - Endpoints : `/api/drones`, `/api/frame/<id>`, `/api/rotate`, `/api/revoke`, `/api/policy`
- `server_source/ui/app.js` : client JS qui consomme ces endpoints

## Description
L'UI operator de HESIA est une page web statique (`server_source/ui/`) servie par un mini serveur Python qui proxifie vers le serveur principal C++. Observations :
- Bind sur `127.0.0.1` uniquement (bonne pratique) MAIS :
- **Aucune authentification** : pas de login, pas de token, pas de CSRF.
- **Pas de HTTPS local** : traffic HTTP en clair sur loopback.
- **Endpoints mutants exposés** : `/api/rotate`, `/api/revoke`, `/api/policy` peuvent modifier l'état sans credential.
- **Pas de CSP** ni CORS strict.

Conséquence : tout process local sur le serveur (low-priv user) peut interroger et **muter** l'état HESIA via l'API UI.

## Impact
- **Escalade de privilèges locale** : un user sans droit `hesia` peut révoquer des drones, forcer la rotation de clés, modifier la policy.
- **CSRF remote** : si l'opérateur visite une page malveillante pendant qu'il utilise l'UI, des requêtes cross-origin peuvent atteindre `127.0.0.1:<port>` (browser même avec Same-Origin, la plupart des navigateurs ne protègent pas contre l'accès localhost depuis des pages externes si CORS permissive).
- **Exfiltration vidéo** : `/api/frame/<id>` retourne les captures (cf. Faille_19).
- **Fingerprinting** : `/api/drones` liste drone_id, attest pub, statut.

## Scénario d'exploitation
**Local** :
```bash
# user lambda sur le serveur HESIA
curl -X POST http://127.0.0.1:8080/api/revoke -d '{"device_id":"drone_001"}'
# drone légitime révoqué, opération détectable mais déjà exécutée
```

**Remote via CSRF** :
```html
<!-- page attaquant, victime = opérateur qui a l'UI ouverte -->
<img src="http://127.0.0.1:8080/api/revoke?device_id=drone_001" />
<!-- ou fetch POST selon CORS -->
```

## Correctif recommandé
1. **Authentification obligatoire** :
   - Token signé stocké dans un cookie `HttpOnly; Secure; SameSite=Strict`.
   - Login via un mot de passe opérateur + 2FA (TOTP) ou webauthn.
2. **CSRF tokens** : header `X-HESIA-CSRF` à chaque mutation.
3. **HTTPS local** : cert auto-signé pinné dans le frontend.
4. **CSP strict** : `default-src 'self'; connect-src 'self' wss:; frame-ancestors 'none'`.
5. **CORS** : `Access-Control-Allow-Origin: null` sauf origine locale explicite.
6. **Rate-limit** par session.
7. **Audit log** sur chaque endpoint mutant.
8. **Déplacer les endpoints mutants** vers un sous-domaine séparé `admin.127.0.0.1` avec authentification renforcée.

## Dépendances
- Faille_18 : mutation sans signature.
- Faille_19 : frames exposées.

## Jetson requis
Non.

## Effort estimé
- 2 semaines dev (auth + CSRF + HTTPS local) + 1 semaine tests.
