# Faille 10 — Clés ML-DSA démo embarquées / publiques dans le dépôt

## Priorité : **P1 — Haute** · Gravité : ~7.0

## Localisation
- `server_source/keys/demo_public.bin` (tracké git, visible publiquement)
- `server_source/keys/demo_secret.bin` (absent du repo courant mais exclu du .gitignore → risque de commit accidentel)
- `server_source/keys/README_KEYS.md` (instructions et chemins)
- `drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c:72-79` : pubkey recovery embarquée en constante (`kHesiaRecoveryPubkey`)
- `server_source/src/main.cpp` : chargement configurable de la clé par env var

## Description
Le dépôt contient des **clés de démonstration** (`demo_public.bin`, `demo_public_ed25519.bin`, `demo_policy.sig`, etc.) qui sont utilisées par défaut si l'opérateur ne fournit pas ses propres clés de production. Problèmes observés :

1. **La clé publique démo est trackée dans git** : un firmware compilé avec les valeurs par défaut intègre cette pubkey → accepte des signatures émises avec la clé privée démo publique (si celle-ci fuit ou a fuité historiquement).
2. **La clé privée démo n'est pas dans .gitignore** (vérifié : le `.gitignore` filtre `*.key`, `*.pem`, `keys/private/`, mais pas spécifiquement `demo_secret.bin`). Un développeur peut la commiter accidentellement.
3. **Fallback silencieux** : aucun `assert` ne vérifie que la pubkey chargée n'est **pas** la pubkey démo. Un binaire release peut embarquer la pubkey démo si la variable d'env n'est pas positionnée.
4. **kHesiaRecoveryPubkey** embarquée en dur : toute rotation de cette clé exige un rebuild complet du TA signé.

## Impact
- **Forge de signature sur firmware/policy** : si la privkey démo est exposée (historique git d'un fork, dépôt miroir sur un builder CI public), un attaquant peut signer une policy arbitraire qui sera acceptée par le drone.
- **Acceptance du bypass** : un binaire avec pubkey démo accepte silencieusement les objets signés démo.
- **Impossibilité de rotation dynamique** : la recovery pubkey étant en dur dans le TA, aucun mécanisme over-the-air ne permet de la remplacer sans re-flash TA.

## Scénario d'exploitation
1. Un contributor open source fork Hesia-Firmware, compile avec paramètres par défaut, flashe un drone de test.
2. Le drone accepte `demo_policy.sig` qui active `require_mtls=0`, `require_secure_boot=0`, etc.
3. Chemin d'attaque cleartext via Faille_03 ouvert.

Ou pour la recovery :
1. Attaquant interne à la chaîne de production se procure la clé privée `recovery` (stockée offline mais manipulée par 1-2 personnes).
2. Pas de possibilité de la rotater sans nouveau flash signé.

## Correctif recommandé
1. **Retirer `demo_public.bin` du tracking git** : `git rm --cached server_source/keys/demo_*`. Les docs doivent les générer à la volée.
2. **Assertion hard au démarrage** :
   ```cpp
   if (loaded_pubkey_sha == kDemoPubkeySha) {
       std::cerr << "REFUSING TO RUN WITH DEMO KEY" << std::endl;
       std::terminate();
   }
   ```
3. **Build flag `HESIA_REFUSE_DEMO_KEYS`** qui lit `demo_public.bin` à la compilation et injecte l'anti-assertion.
4. **Rotater la recovery pubkey** via un mécanisme à deux niveaux : une `root_recovery` et une `intermediate_recovery` rotatable par signature root.
5. **Ajouter `demo_*.bin` au .gitignore** explicitement.
6. **Hook pre-commit** qui détecte les extensions `.bin` > 64 octets et refuse le commit.

## Dépendances
- Faille_02 : bootstrap TA accepte un secret de 32 octets → attaquant qui a la recovery démo devient owner.
- Faille_03 : binaire avec policy démo → TLS désactivé.

## Jetson requis
Non (validation sur build release + inspection git log).

## Effort estimé
- 2 jours dev + 1 semaine de ceremony pour générer et custody les vraies clés production.
