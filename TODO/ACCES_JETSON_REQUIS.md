# Vérifications nécessitant un accès au Jetson

Ce document liste les contrôles **qui ne peuvent pas être conclus depuis ce poste** et exigent un accès physique ou SSH au Jetson Orin Nano Super (`ajax-desktop` / `100.101.152.53`). L'audit actuel a été réalisé en **lecture de code uniquement**.

## 1. Chaîne Secure Boot et fuses

**Pourquoi** : vérifier si la chaîne Secure Boot hardware est effectivement active, que les fuses ODM UID sont bien "burn", que l'UEFI/kernel/initramfs sont signés par une chaîne de confiance fermée.

**Nécessite** :
- Accès root sur le Jetson
- Lecture de `/sys/class/tegra-fuse/*` et outils `tegra-ota` / `nvbootfuse`
- Lecture de `/proc/cmdline` pour vérifier `secure_boot=1`, modes de boot
- Dump du bootloader via `tegrarcm_v2` (mode RCM) — exige câble micro-USB et mise en mode recovery
- Comparaison avec la preuve BSP NVIDIA

**Enjeu** : `SECURITY_VALIDATION_PLAYBOOK.md` admet que ce contrôle reste à faire.

## 2. État réel RPMB et stockage persistant TEE

**Pourquoi** : Faille_01 conclut que l'absence de RPMB rend toute l'anti-replay TEE inopérante. Confirmer sur la cible réelle que :
- `/dev/mmcblk*rpmb` n'existe effectivement pas sur ce Jetson SD-boot
- Le SFS OP-TEE est bien dans `/data/tee/` (REE-backed)
- Tester concrètement le rollback : snapshot `/data/tee/`, rotate, restore, observer comportement

**Nécessite** :
- Accès root SSH
- Capacité de reboot

## 3. Packaging et provenance OP-TEE / TA

**Pourquoi** : le TA déployé peut-il avoir été compilé avec `HESIA_TA_ENABLE_MAINTENANCE_CMDS=1` (Faille_11) ? Quel est le hash SHA3-512 du binaire TA déployé ? Qui signe le TA ? La clé de signature TA est-elle stockée offline ?

**Nécessite** :
- Accès `/lib/optee_armtz/*.ta` ou équivalent Jetson
- Déroulement du workflow `tegra-signimage` / `tegrasign_v3` ou équivalent
- Traçabilité du build-host TA

## 4. Configuration systemd réelle

**Pourquoi** : les unit files dans `security/systemd/` déclarent un durcissement, mais il faut vérifier sur la cible que :
- `systemd-analyze security hesia-drone.service` donne bien score ≤ 3.0
- `EnvironmentFile=-/etc/hesia/hesia.env` n'a pas de drop-in override malveillant
- `HESIA_FORENSIC_*` ne sont pas activées par défaut

**Nécessite** :
- `systemctl cat hesia-drone.service` et scan `/etc/systemd/system/hesia-*.service.d/`
- Test actif des `HESIA_*` env variables en prod_fuse pour vérifier qu'elles sont bien ignorées

## 5. Permissions effectives sur `secure_dir`

**Pourquoi** : Faille_21 (frames déchiffrées) et Faille_22 (backup rotate_all_keys) montrent un risque de fuites par permissions laxistes. Il faut vérifier :
- `stat -c '%a %U %G' /etc/hesia/secure/*`
- `getfacl /etc/hesia/secure`
- Présence de `chattr +i` sur les blobs runtime
- Absence de symlinks vers /tmp ou /home

## 6. Comportement réel anti-rejeu GCM

**Pourquoi** : Faille_04 (iv_prefix 32-bit) et Faille_08 (anti-replay après décrypt) doivent être testées end-to-end :
- Tracer les IVs utilisés sur une vraie session (debug flag ou instrumentation locale)
- Mesurer le coût CPU d'un replay flood
- Confirmer que `ConstantTime::equals` compile avec `_mm_lfence` actif sur aarch64

**Nécessite** :
- Compilation d'un build debug spécifique avec instrumentation
- Outils type `perf`, `strace`, `bpftrace`

## 7. OP-TEE bootstrap réel

**Pourquoi** : Faille_02 prétend qu'un attaquant REE peut bootstrap la session-auth avec deux constantes magic publiques. Il faut reproduire sur Jetson :
- Wipe `/data/tee/*`
- Tenter le bootstrap avec les constantes `HESIA_BOOTSTRAP_MAGIC_A/B`
- Observer la réponse du TA

**Nécessite** :
- TA réel déployé
- Capacité de wipe contrôlée

## 8. Effet obfuscation / cloaking sur la reverse-engineering

**Pourquoi** : `build_hesia_cloaked_release.sh` active `HESIA_ENABLE_RELEASE_CLOAKING=ON` mais l'efficacité n'est pas mesurable sans le binaire stripped réel sur Jetson :
- Tentative de symbolisation du binaire déployé via IDA/Ghidra
- Comparer vs un build non-cloaked
- Mesurer la perte de performance réelle (latence YOLO/MiDaS)

**Nécessite** :
- Accès au binaire `/opt/hesia/bin/hesia_drone` + `.debug` séparé
- Reverse tooling sur une copie locale

## 9. Validation réelle des artefacts de soak test

**Pourquoi** : `artifacts/jetson_transport_soak/2026-04-23_20-23-52_final/summary.json` est cité comme preuve mais pas auditable depuis ce poste sans cross-check logs Jetson :
- Vérifier timestamps journalctl correspondent
- Vérifier absence de frames `Drop frame (queue full)` masquées

**Nécessite** :
- `journalctl --since "2026-04-23" -u hesia-*.service` sur le Jetson

## 10. Surface d'attaque physique

**Pourquoi** : console série UART, JTAG, debug ports, état fusibles JTAG-disable — non inspectables depuis ce poste :
- Vérifier `jtag_disable` fuse
- Vérifier présence/absence de headers série exposés sur PCB
- Vérifier chiffrement eMMC (ici SD donc probablement non chiffré)

**Nécessite** :
- Inspection physique du board
- Pentester matériel (outils type ChipWhisperer, JTAGulator, PCIe analyzer)

---

## Résumé des accès à demander

Pour compléter un audit sérieux, il faut **a minima** :

1. **SSH root** sur le Jetson (`ajax@100.101.152.53` via la clé SSH fournie — mais cf. Faille_30 sur cette clé)
2. **Accès physique** au Jetson (console série, mode recovery USB)
3. **Accès au build pipeline** pour vérifier la provenance binaire
4. **Accès aux logs journalctl complets** sur la fenêtre de soak test revendiquée
5. **Accès à la signature TA** workflow pour vérifier son offline-ness
6. **Une heure de fenêtre de test** en mode invasif (wipe + re-bootstrap) pour reproduire Faille_01, Faille_02, Faille_13 concrètement
