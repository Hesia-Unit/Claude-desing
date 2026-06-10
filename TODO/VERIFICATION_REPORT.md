# HESIA-Firmware - Rapport de verification des correctifs

Date de verification: `2026-04-25`

Portee:
- 25 failles nominatives `Faille_01` a `Faille_25`
- code courant du depot
- rebuilds critiques deja rejoues sur le Jetson `ajax-desktop`
- validation cible via `hesia-validate.sh`
- test unitaire `hesia_secure_channel_tests`
- soak post-correctifs

Artefacts de preuve:
- [summary.json](/C:/Users/matis/Documents/Hesia-Firmware/artifacts/jetson_transport_soak/2026-04-25_09-21-44/summary.json)
- [report.md](/C:/Users/matis/Documents/Hesia-Firmware/artifacts/jetson_transport_soak/2026-04-25_09-21-44/report.md)
- image hors-ligne Jetson: [F:\Image-Jetson\2026-04-24_22-19-09\reports\README.md](F:\Image-Jetson\2026-04-24_22-19-09\reports\README.md)

## Synthese

Sur les 25 failles auditees:

- `21` sont corrigees
- `3` restent partielles
- `1` reste non corrigee
- `0` restent incertaines

## Tableau de statut

| ID | Titre court | Priorite | Statut |
|---|---|---:|---|
| Faille_01 | RPMB absent / rollback TEE | P0 | NON CORRIGEE |
| Faille_02 | Bootstrap TOFU magic | P0 | CORRIGEE |
| Faille_03 | TLS fallback clair | P0 | CORRIGEE |
| Faille_04 | AES-GCM nonce 32-bit | P0 | CORRIGEE |
| Faille_05 | HKDF-HMAC-SHA3 maison | P0 | CORRIGEE |
| Faille_06 | TA sealing sans AAD | P1 | CORRIGEE |
| Faille_07 | Pas de split sender/receiver | P1 | CORRIGEE |
| Faille_08 | Anti-replay post-decrypt | P1 | CORRIGEE |
| Faille_09 | decrypt resize non borne | P1 | CORRIGEE |
| Faille_10 | Cles demo embarquees | P1 | CORRIGEE |
| Faille_11 | wipe_key_internal partiel | P1 | CORRIGEE |
| Faille_12 | Attestation P-256 legacy non HW-bound | P1 | CORRIGEE |
| Faille_13 | Recovery nonce replay | P1 | PARTIELLE |
| Faille_14 | Commandes maintenance sans signature offline | P1 | CORRIGEE |
| Faille_15 | sign_attest_digest < 32B | P1 | CORRIGEE |
| Faille_16 | Accept loop DoS | P1 | CORRIGEE |
| Faille_17 | KDF SP800-108 separateur ambigu | P1 | CORRIGEE |
| Faille_18 | Allowlist sans signature | P1 | CORRIGEE |
| Faille_19 | Frames world-readable | P1 | CORRIGEE |
| Faille_20 | env HESIA_FORENSIC bypass | P1 | CORRIGEE |
| Faille_21 | shell injection rotate_all_keys | P1 | CORRIGEE |
| Faille_22 | UI non authentifiee | P1 | CORRIGEE |
| Faille_23 | Path traversal video | P1 | CORRIGEE |
| Faille_24 | Secrets non zeroises RAM | P1 | PARTIELLE |
| Faille_25 | Cle SSH privee sur disque | P1 | PARTIELLE |

## Justification des reclassements

### Faille_12 - Attestation P-256 legacy non HW-bound

Statut: `CORRIGEE`

Constat:
- le drone refuse en production une attestation TEE hors chemin ML-DSA in-TEE
- le serveur rejette explicitement la voie legacy P-256 en mode production

Preuves code:
- [hesia_drone.cpp](/C:/Users/matis/Documents/Hesia-Firmware/drone_source/hesia_drone.cpp)
- [hesia_server_session.cpp](/C:/Users/matis/Documents/Hesia-Firmware/server_source/src/hesia_server_session.cpp)

Conclusion:
- le probleme de la cle legacy P-256 reste vrai comme limite historique
- mais il n'est plus accepte par le systeme cible de production audite

### Faille_14 - Commandes maintenance sans signature offline

Statut: `CORRIGEE`

Constat:
- les commandes de maintenance dangereuses ne sont plus ouvertes dans le profil deploiement courant
- elles sont conditionnees par `HESIA_TA_ENABLE_MAINTENANCE_CMDS`
- la table de dispatch en production n'expose pas les commandes critiques citees par l'ancien audit

Preuve code:
- [ta_hesia.c](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c)

Conclusion:
- l'absence de ceremonie de signature offline reste un hardening desirable pour un futur profil maintenance
- mais la faille telle qu'auditee n'est plus exploitable sur le systeme cible actuel

### Faille_19 - Frames world-readable

Statut: `CORRIGEE`

Constat:
- les ecritures UI/forensics sont maintenant creees avec permissions owner-only
- les repertoires associes sont durcis
- `latest.jpg`, `frame_meta.json` et les captures forensics ne sont plus world-readable

Preuve code:
- [hesia_server_session.cpp](/C:/Users/matis/Documents/Hesia-Firmware/server_source/src/hesia_server_session.cpp)

Conclusion:
- le chiffrement at-rest et la retention sont encore du hardening additionnel utile
- mais le coeur de la faille nommative est ferme

### Faille_22 - UI non authentifiee

Statut: `CORRIGEE`

Constat:
- token Bearer obligatoire sauf opt-in debug local explicite
- refus du bind distant sans token
- CSP, `X-Frame-Options`, `nosniff`, `Referrer-Policy`, rate limiting deja en place
- TLS serveur local ajoute dans [ui_server.py](/C:/Users/matis/Documents/Hesia-Firmware/server_source/tools/ui_server.py) avec refus du bind distant sans certificat si TLS est requis
- les endpoints mutants cites par l'ancien audit ne sont plus presents dans l'outil courant

Conclusion:
- un vrai parcours operateur avec 2FA reste un chantier produit separe
- la faille d'API locale ouverte sans auth n'est plus representatrice de l'etat actuel

## Points encore partiels

### Faille_13 - Recovery nonce replay

Statut: `PARTIELLE`

En place:
- nonce unique persistant et consomme
- hash de la cle d'attestation courante verifie
- signature recovery verifiee
- TTL TEE cote challenge/token (`expires_at_sec`) et verification stricte de l'expiration
- etat de recovery persiste en v2 avec expiration liee au nonce

Reste:
- pas d'ancre monotone hardware
- pas de RPMB sur cette cible SD
- donc un rollback du stockage TEE reste un residu structurel

### Faille_24 - Secrets non zeroises RAM

Statut: `PARTIELLE`

En place:
- `OPENSSL_cleanse`
- `mlock`
- `MADV_DONTDUMP` / `MADV_DODUMP` sur les buffers critiques proteges
- `RLIMIT_CORE=0`
- zeroization explicite sur plusieurs buffers critiques
- `SecureMemory::protect()` etendu aux blobs scelles OP-TEE et aux secrets retournes par Kyber/Dilithium

Reste:
- pas de `secure_allocator` generalise sur tout le graphe d'objets secrets
- pas de `MADV_DONTDUMP` buffer par buffer partout

### Faille_25 - Cle SSH privee sur disque

Statut: `PARTIELLE`

En place:
- le depot ignore `key-ssh/`
- les scripts supportent les cles hors repo via `~/.ssh`

Reste:
- la cle physique reste materialisee sur poste de dev
- la garde physique et les ACL poste restent un sujet operationnel, pas firmware

## Point non corrige

### Faille_01 - RPMB absent / rollback TEE

Statut: `NON CORRIGEE`

Justification:
- la cible Jetson Orin Nano Super utilise ici un support SD sans RPMB expose
- aucun fuse monotone ou secure element additionnel n'est disponible dans cette revision
- ce point n'est pas corrigable honnetement par simple patch firmware sur cette cible

## Validation cible reappliquee

Build et tests deja verifies sur cible:
- rebuild TA OP-TEE: `OK`
- rebuild host tool OP-TEE: `OK`
- rebuild `hesia_drone`: `OK`
- rebuild `hesia_server_cpp`: `OK`
- test unitaire `hesia_secure_channel_tests`: `OK`
- redéploiement live `hesia_drone` / `hesia_server_cpp` / TA: `OK`

Validation Jetson:
- `hesia-validate.sh`: `Pass: 28 / Warn: 5 / Fail: 0`
- `ldd /opt/hesia/bin/hesia_drone`: `libhesia_sentinel.so => /opt/hesia/bin/../lib/libhesia_sentinel.so`
- `readelf -d /opt/hesia/bin/hesia_drone`: `NEEDED=libhesia_sentinel.so`, `RUNPATH=$ORIGIN/../lib:/usr/local/cuda/lib64`

Warnings restants:
- `require_boot_measure=0`
- `require_asset_manifest=0`
- `require_ab_slots=0`
- `require_rpmb_rollback_storage=0`
- absence de RPMB sur support SD

Soak post-correctifs:
- serveur actif sur `100%` des echantillons
- drone actif sur `100%` des echantillons
- session continue `SERVERCPP.100.101.152.53:53180` stable sur toute la fenetre capturee
- `165` telemetry OK
- `54` `VIDEO_DATA ok`
- `0` echec decrypt
- `0` echec TLS
- `0` transport failure drone
- `0` queue drop drone

Correctifs finaux Jetson appliques pendant cette passe:
- `seccomp`: autorisation de `mlock` / `munlock` / `mlock2` et ouverture `mmap` / `mprotect` executable uniquement pour le profil `DRONE_OPERATIONAL`, necessaire au runtime TensorRT/CUDA du Jetson
- `CMake` Sentinel: `IMPORTED_NO_SONAME` + `IMPORTED_SONAME=libhesia_sentinel.so` pour eliminer le `DT_NEEDED` absolu vers le workspace
- binaire deploye patchelf:
  - `RUNPATH=$ORIGIN/../lib:/usr/local/cuda/lib64`
  - plus aucune dependance executable vers `/home/ajax/.cache/.hesia`

## Conclusion

Le rapport precedent n'etait plus a jour et sous-estimait plusieurs corrections deja actives dans le code et sur la cible.

Etat honnete actuel:
- `21 / 25` corrigees
- `3 / 25` partielles
- `1 / 25` non corrigee
- `0 / 25` incertaine

Le residu principal reste materiel:
- absence d'ancrage monotone hardware sur ce Jetson SD sans RPMB

Les autres residus sont du hardening incrementiel ou de l'operational security hors firmware pur.
