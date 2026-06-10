# AGENTS

Codex peut utiliser `gemini-cli` comme pool d'agents de travail.

## Regle generale

- Codex est l'orchestrateur.
- Les instances `gemini` sont des agents executants.
- Codex peut ouvrir autant de terminaux que necessaire et lancer `gemini --yolo` dans chacun.
- Il n'y a pas de limite pratique fixe au nombre d'agents tant que la delegation reste utile et lisible.
- Chaque tache donnée à une instance `gemini` devra être précédé du folder dans le quelle il évolura et de la regle suivante 'tu a tous droit sur les fichier de ton dossier (ecriture/lecture, execution/suppression) mais tous fichier en dehors de ce folder ne peuvent pas etre réécrit directement par toi ou supprimer'
- Chaque instance aura sont propre dossier d'évolution avec un todo.md, la création du dossier est a ta charge et dois etre fais avant appel de l'agent 
- Le téléchargement de dependance est autoriser pour tous les agents
- Les agents evoluront tous sur le dsique F:\ et enregistreront leur travaux final sur le disque C:\Users\matis\Documents\Hesia-Firmware
- Outre pour les tache de théorisation et de mise en place du plan qui te sont assigner, limite au maximum le think pour miniser la limite du model et maximiser la durer des tache.

## Utilisation

- Pour lancer un agent, ouvrir un terminal et taper `gemini --yolo`.
- Refaire l'operation autant de fois que necessaire pour creer plusieurs agents en parallele.
- Donner a chaque agent une mission claire, courte, et borneee.

## Missions adaptees

- Ecriture de code
- Revue de code
- Debug
- Refactor
- Redaction de documentation
- Analyse de logs
- Proposition de tests

## Mode de travail

- Codex théorise et créer un plan de travail pour chaque objectif 
- Codex decoupe le travail.
- Codex assigne une tache differente a chaque agent `gemini`.
- Les agents travaillent en parallele.
- Codex recupere les resultats, tranche, integre, verifie, puis livre le resultat final.

## Discipline

- Un agent par sujet ou sous-probleme.
- Eviter de donner la meme tache a deux agents sauf si Codex veut comparer deux approches.
- Preferer des consignes concretes: fichier, bug, fonctionnalite, test, revue.
- Codex garde la responsabilite finale sur l'architecture, la coherence, la qualite, et la livraison.

## Warning

Do not stop before all executable work for this phase is completed or a hard external blocker is proven.
Every claim must be backed by logs, files, measurements, builds, or direct remote inspection.
Do not emulate missing security features. If a platform feature is unavailable, document the real limitation and adapt policy explicitly for this Jetson-only version.
Continuous execution mode is active: keep progressing autonomously across remaining objectives until the user explicitly stops the run.

## Objectif

- Alpha : Nouvelle architecture d'intelligence artificielle multimodale novatrice pour les systèmes embarqués
- Beta : Modélisation complète d'un aéronef 
- Gamma : Amélioration Github et crédibilise professionnel
- Epsilon : Analyse et résumer ECE Nsi 

## Alpha 

- Contrainte principal : Les couche linéaire doivent reprend le 1 bits des model bitnet pour limité au maximum la taille memoir et les besoin en calcule 
- Contrainte principal : Reprendre la comprhension temporal implicite du model comme pour les model Mamba + inference O(n)
- Contrainte principal : Applique la théorie du ticket gagnant pour limiter au maximum la taille du model 

- Matèriel mis a disposition : RTX 3070 8GO de Vram
- Matèriel mis a disposition : Ryzen 7 5800x
- Matèriel mis a disposition : 24go Ram 
- Matèriel mis a disposition : Huggingface connecter a Codex
- Matèriel mis a disposition : Internet 

- Donnée complémentaire : En abscence de platforme d'inference autre que la rtx 3070, tous les benchmark se feront sur ce gpu, Compare des modes comparable de manière général, ne biaise pas les données.

- Attente Vision : DINOv3
- Attente inference : Mamba-2
- Attente taille : < YoloV8s
- Attente verbal : ≃ des model du marchée 

- Rendu : Papier de recherche professionel (Anglais et Français), model complet -> script d'entrainement et d'inference, jeux d'entrainement venant de Huggingface

## Beta 

- Contrainte principal : Topologie complète (Battrie, Jetson orin nano, Pixhawk, chambre rf, EDF, découpage pièce, fonctionnement et action des train d'atterissage et des volets) -> le choix de la battrie, de l'EDF et des dimention sont a choisir par toi
- Contrainte principal : Furtiviter niveau B-2/F-117 
- Contrainte principal : Le model doit être constructible 

- Matèriel mis a disposition : RTX 3070 8GO de Vram
- Matèriel mis a disposition : Ryzen 7 5800x
- Matèriel mis a disposition : 24go Ram 
- Matèriel mis a disposition : Blender 5.1, Fusion 360
- Matèriel mis a disposition : Kali linux via wsl 
- Matèriel mis a disposition : Internet 

- Donnée complémentaire : la video sous different angle de l'aéronef est dans le folder C:\Users\matis\Documents\Hesia-Firmware\Modelisation

- Warning : Ne pas effectuer cette tache en parallèle de la tache Alpha en raison de la charge Gpu 

- Rendu : fichier STL, .OBJ, graphique RCS de la struture dans le pire et le meilleur cas ainsi que les psect viser, liste complet des composant (liste de course)
## Gamma

- Donnée complémentaire : Le est accesible via ssh a l'adresse https://github.com/Hesia-Unit

- Rendu : 1 pdf récapitulatif

##  Epsilon

- Effectue un pdf récapitulatif pour chaque sujet du folder C:\Users\matis\Documents\NSI\Sujet Nsi -> (sur quoi cela porte les reponse a apporter, et quelle script faire (logique du script et exemple))

- Rendu : 23 PDF

