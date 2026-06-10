# HESIA Alpha : architecture multimodale ternaire à espace d'états pour l'autonomie embarquée

Date : 2026-05-09

## Résumé

HESIA Alpha propose une architecture multimodale compacte pour l'autonomie embarquée. Elle combine trois leviers d'efficacité : projections linéaires ternaires de type BitNet, modélisation temporelle inspirée de Mamba-2, et élagage par ticket gagnant. Le modèle vise un cadre matériel réaliste : expériences sur RTX 3070, déploiement ultérieur Jetson Orin Nano, mémoire limitée et inférence temps réel. L'état actuel est un prototype de recherche borné, pas un checkpoint final entraîné. Cette phase convertit les projections linéaires principales en `BitLinear`, ajoute un script d'inférence et définit les sources Hugging Face pour la distillation DINOv3 et les données UAV.

## Motivation

Un modèle embarqué doit comprendre l'image, l'état du véhicule et la dynamique temporelle sans supporter le coût mémoire d'un grand transformer. Alpha garde donc un étudiant compact pour le déploiement et réserve les grands modèles à l'entraînement. DINOv3 sert de professeur visuel, Mamba-2 inspire la récurrence en temps linéaire, et BitNet b1.58 inspire les projections ternaires à faible empreinte mémoire.

## Travaux Reliés

DINOv3 est retenu comme professeur visuel, pas comme backbone embarqué. Hugging Face expose notamment `facebook/dinov3-vits16-pretrain-lvd1689m`, `facebook/dinov3-vitb16-pretrain-lvd1689m` et `facebook/dinov3-convnext-tiny-pretrain-lvd1689m`.

Mamba-2 repose sur la dualité structurée des espaces d'états. L'article "Transformers are SSMs" présente un coeur Mamba-2 comme un raffinement des SSM sélectifs, avec un gain annoncé de 2 à 8 fois tout en restant compétitif en modélisation du langage.

BitNet b1.58 représente les poids par les valeurs ternaires `{-1, 0, 1}`, soit environ 1,58 bit par poids. Alpha applique cette idée aux projections linéaires avec une couche `BitLinear` et un estimateur straight-through.

La théorie du ticket gagnant motive l'élagage itératif par magnitude : entraîner dense, identifier un sous-réseau, revenir à un état de départ, puis réentraîner uniquement les poids conservés.

## Architecture

Le candidat Alpha s'appuie sur le modèle HESIA M2B existant :

- Les flux RGB et profondeur sont encodés par de petits encodeurs convolutionnels.
- Les caractéristiques visuelles sont fusionnées puis condensées en token.
- L'état véhicule et l'embedding mission sont ajoutés au token visuel.
- Une pile de blocs SSM sélectifs traite la séquence temporelle en temps linéaire.
- Les têtes produisent les actions futures, le risque et des heatmaps.

La variante mission ajoute les logits sémantiques, les masques d'instances, les classes d'instances et les logits d'étape.

Dans cette phase, les projections linéaires principales des modèles base et mission ont été converties en `BitLinear` : projections SSM d'entrée, delta, B, C, D, sortie, projections visuelles et état, tête de risque, tête d'étape et tête de classes d'instances. Les convolutions restent denses pour le moment, car leur quantification et leur calibration TensorRT relèvent d'une étape séparée.

## Plan De Distillation DINOv3

Alpha ne déploie pas DINOv3 directement. Le plan est :

1. Charger un professeur DINOv3 sur RTX 3070 pour extraire des caractéristiques hors ligne.
2. Entraîner l'encodeur visuel compact à imiter les caractéristiques globales et denses du professeur.
3. Mélanger la perte de distillation avec les pertes mission, action et risque.
4. Exporter uniquement l'étudiant compact vers ONNX/TensorRT.

Cette stratégie conserve l'apport d'un modèle visuel fondationnel tout en respectant la contrainte mémoire embarquée.

## Données D'entraînement

Le manifeste Hugging Face initial contient :

- `Meehai/dronescapes`
- `Meehai/dronescapes2`
- `Voxel51/dronescapes2_annotated_train_set`
- `pathikg/drone-detection-dataset`
- `lgrzybowski/seraphim-drone-detection-dataset`

Ces sources couvrent vidéo UAV, scènes drone multimodales, segmentation légère et détection d'objets/drone. Chaque source doit passer une revue de licence avant publication de checkpoints dérivés.

## Inférence Et Élagage

Le nouveau script d'inférence est :

```bash
python -m ml.hesia_m2b.infer --checkpoint artifacts/hesia_m2b_closed_loop.pt --out artifacts/hesia_m2b_infer.json
```

Le flux d'entraînement existant supporte déjà le ticket gagnant : masques, pruning global par fraction, capture de l'état de retour, export de checkpoint masqué. Le prochain benchmark doit comparer checkpoint dense et checkpoint élagué avec mêmes formes d'entrée, longueur de séquence, batch size et environnement RTX 3070.

## Preuves Actuelles

Fichiers implémentés :

- `ml/hesia_m2b/bitlinear.py`
- `ml/hesia_m2b/model.py`
- `ml/hesia_m2b/mission_model.py`
- `ml/hesia_m2b/infer.py`
- `ml/hesia_m2b/hf_alpha_manifest.py`

Vérifications exécutées :

```bash
python -m py_compile ml\hesia_m2b\bitlinear.py ml\hesia_m2b\model.py ml\hesia_m2b\mission_model.py ml\hesia_m2b\hf_alpha_manifest.py ml\hesia_m2b\infer.py
python ml\hesia_m2b\hf_alpha_manifest.py
```

Torch n'est pas installé dans l'environnement Python de base actuel. Aucun entraînement ni benchmark runtime n'est donc revendiqué pour cette phase.

## Limites

Le bloc SSM est inspiré de Mamba-2 mais n'est pas le kernel upstream Mamba-2. Le chemin DINOv3 est un plan de distillation et un manifeste de sources, pas encore un entraînement professeur-étudiant complet. La performance verbale comparable aux modèles du marché n'est pas revendiquée : elle demande une tête langage, des tâches d'évaluation et des benchmarks réels. Le déploiement Jetson reste une phase suivante après validation CUDA/PyTorch et TensorRT.

## Références

- Page papier et collection DINOv3 : https://huggingface.co/papers/2508.10104
- Professeur DINOv3 ViT-S/16 : https://hf.co/facebook/dinov3-vits16-pretrain-lvd1689m
- Professeur DINOv3 ConvNeXt-Tiny : https://hf.co/facebook/dinov3-convnext-tiny-pretrain-lvd1689m
- Mamba-2 / State Space Duality : https://arxiv.org/abs/2405.21060
- BitNet b1.58 : https://arxiv.org/abs/2402.17764
- Théorie du ticket gagnant : https://huggingface.co/papers/1803.03635

## Addendum : Execution Alpha

Apres le papier d'architecture initial, Alpha a ete entraine et benchmarke sur la RTX 3070 locale.

Le checkpoint executable est `artifacts/alpha/hesia_alpha_distilled.pt`. Il contient 706 067 parametres, 31 couches `BitLinear`, zero couche dense `nn.Linear`, et un masque ticket gagnant a 32,5% de sparsity. Le checkpoint pese 3,62 MB et l'export ONNX verifie pese 2,99 MB.

L'entrainement synthetique CUDA avec pruning ticket gagnant descend a une loss finale de 0,0254. L'echantillonnage Hugging Face a collecte 192 images drone depuis `Voxel51/dronescapes2_annotated_train_set` et `pathikg/drone-detection-dataset`. Le chargement direct de DINOv3 depuis `facebook/dinov3-vits16-pretrain-lvd1689m` a ete tente, mais Hugging Face a renvoye une erreur 401 de depot gate parce que l'environnement local n'est pas authentifie pour ce modele. Le professeur de repli executable est `facebook/dinov2-small`.

La distillation vision atteint 0,4451 de similarite cosinus sur le split de validation apres quatre epochs. Sur le sous-ensemble de benchmark, la latence full forward HESIA est de 11,12 ms contre 14,81 ms pour l'extraction de features DINOv2-small. Le coeur temporel est un SSM selectif portable en O(n), mais il reste plus lent qu'un petit transformer PyTorch optimise en microbenchmark; le gain de vitesse Mamba-2 n'est donc pas revendique.

Les objectifs de taille et de projections BitLinear sont atteints pour le prototype. La vision niveau DINOv3 et le verbal comparable aux modeles du marche ne sont pas revendiques a ce stade. Le premier point demande un acces DINOv3 authentifie; le second demande un decodeur langage entraine ou un adaptateur vers un petit LLM.
