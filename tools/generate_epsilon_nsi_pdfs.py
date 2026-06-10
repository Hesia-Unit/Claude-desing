#!/usr/bin/env python3
"""Generate concise revision PDFs for the 2026 NSI practical subjects.

The script reads the source folders, extracts objective evidence from the
provided PDFs and Python files, then renders one recap PDF per subject.
"""

from __future__ import annotations

import ast
import html
import re
import sys
from pathlib import Path

import fitz
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    BaseDocTemplate,
    Frame,
    HRFlowable,
    KeepTogether,
    PageTemplate,
    Paragraph,
    Preformatted,
    Spacer,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = Path(r"C:\Users\matis\Documents\NSI\Sujet Nsi")
DEFAULT_OUTPUT = ROOT / "artifacts" / "epsilon_nsi"
GENERATOR_NAME = "generate_epsilon_nsi_pdfs.py"


SUBJECTS: dict[str, dict[str, object]] = {
    "01": {
        "title": "Codage RLE d'images en niveaux de gris",
        "focus": "Compression sans perte par run-length encoding, décodage et cas des longues suites de pixels.",
        "expected": [
            "Dire que le RLE n'est pas toujours plus court: une image très alternée peut doubler la taille.",
            "Ecrire `decodage_rle` en lisant les couples `(compte, valeur)` et en répétant chaque valeur.",
            "Tester les deux images fournies et constater l'échec quand un compte dépasse 255.",
            "Corriger l'encodage en découpant une longue série en plusieurs couples de taille maximale 255.",
        ],
        "logic": [
            "Parcourir la liste source et compter les valeurs consécutives identiques.",
            "En décodage, avancer de deux en deux dans la liste RLE.",
            "Pour chaque compte, ajouter `compte` copies de `valeur` au résultat.",
            "Limiter chaque compte à 255 si le stockage se fait sur un octet.",
        ],
        "example": "def decodage_rle(code):\n    pixels = []\n    for i in range(0, len(code), 2):\n        pixels.extend([code[i + 1]] * code[i])\n    return pixels",
    },
    "02": {
        "title": "Analyse de salaires et k plus proches voisins",
        "focus": "Dictionnaires, filtrage conditionnel, moyennes, tests et biais possible dans une prédiction par proximité.",
        "expected": [
            "Calculer une moyenne de salaire sur les employés qui vérifient une condition.",
            "Compter les effectifs par sexe avec un dictionnaire `{'F': ..., 'M': ...}`.",
            "Corriger l'écart de salaire quand un groupe est absent et borner le pourcentage.",
            "Comparer les propositions de salaire KNN pour deux profils identiques sauf le sexe.",
        ],
        "logic": [
            "Filtrer les dictionnaires avec `employe[champ] == valeur`.",
            "Renvoyer `None` si le filtre produit une liste vide.",
            "Pour le KNN, trier les distances et moyenner les salaires des `k` plus proches.",
            "Tester les cas limites: un seul sexe, égalité, champ absent dans le raisonnement.",
        ],
        "example": "def salaire_moyen_condition(employes, champ, valeur):\n    salaires = [e['salaire'] for e in employes if e[champ] == valeur]\n    return None if not salaires else sum(salaires) / len(salaires)",
    },
    "03": {
        "title": "Calendrier de cycles et dates",
        "focus": "Calcul de dates, années bissextiles, phases de cycle et génération de format iCalendar.",
        "expected": [
            "Coder `est_bissextile` avec la règle 400/100/4.",
            "Associer un jour de cycle entre 1 et 28 à une phase et protéger l'entrée par assertion.",
            "Ajouter des tests pertinents à `ajouter_jours`, surtout fins de mois et années bissextiles.",
            "Corriger la chaîne iCalendar pour produire un calendrier valide et chronologique.",
        ],
        "logic": [
            "Utiliser des tuples ou listes `(jour, mois, annee)` et la fonction `jours_dans_mois`.",
            "Avancer jour par jour ou par boucle tant que le nombre de jours à ajouter n'est pas épuisé.",
            "Générer chaque évènement iCalendar avec balises obligatoires et fins de ligne cohérentes.",
        ],
        "example": "def est_bissextile(annee):\n    return annee % 400 == 0 or (annee % 4 == 0 and annee % 100 != 0)",
    },
    "04": {
        "title": "Culture de plantes et nettoyage de mesures",
        "focus": "Objets simples, listes de mesures, dictionnaires de regroupement et correction d'une purge logique.",
        "expected": [
            "Calculer la durée moyenne de croissance, avec `None` pour une liste vide.",
            "Construire un dictionnaire qui associe chaque plante à ses mesures.",
            "Identifier pourquoi `purger_mesures_extremes` supprime mal ou saute des éléments.",
            "Corriger la purge sans modifier la liste pendant l'itération de façon dangereuse.",
        ],
        "logic": [
            "Sommer `plante.duree_croissance` et diviser par le nombre de plantes.",
            "Initialiser toutes les clés de plantes avant d'ajouter les mesures.",
            "Produire une nouvelle liste filtrée plutôt que supprimer dans la liste parcourue.",
        ],
        "example": "def croissance_moyenne(plantes):\n    if not plantes:\n        return None\n    return sum(p.duree_croissance for p in plantes) / len(plantes)",
    },
    "05": {
        "title": "Empreinte carbone en JSON imbriqué",
        "focus": "Parcours de dictionnaires, récursivité, somme de valeurs et détection de valeurs aberrantes.",
        "expected": [
            "Sommer les valeurs d'un dictionnaire plat avec `total_simple`.",
            "Ecrire `total_rec` récursive pour additionner toutes les valeurs numériques imbriquées.",
            "Expliquer que la détection aberrante échoue si elle ne descend pas dans les sous-dictionnaires.",
            "Proposer des tests avec dictionnaire plat, profond, sans dépassement et avec dépassement.",
        ],
        "logic": [
            "Pour chaque valeur: si c'est un dictionnaire, appeler récursivement la fonction.",
            "Sinon, si c'est numérique, l'ajouter au total ou la comparer au seuil.",
            "Retourner une information exploitable: booléen, liste de chemins ou messages d'alerte.",
        ],
        "example": "def total_rec(d):\n    total = 0\n    for valeur in d.values():\n        total += total_rec(valeur) if isinstance(valeur, dict) else valeur\n    return total",
    },
    "06": {
        "title": "Boutique de smoothies",
        "focus": "Recettes, ensembles d'ingrédients disponibles, scores de proximité et choix d'alternative.",
        "expected": [
            "Déterminer si tous les ingrédients d'une recette sont disponibles.",
            "Lister toutes les recettes réalisables par la boutique.",
            "Ajouter des tests au score de proximité entre recettes.",
            "Corriger `plus_proche_possible` pour garder le meilleur score parmi les smoothies possibles.",
        ],
        "logic": [
            "Transformer les ingrédients en ensembles et utiliser `issubset`.",
            "Parcourir le dictionnaire des recettes et filtrer celles qui sont possibles.",
            "Initialiser correctement `meilleur_score` et mettre à jour score et nom en même temps.",
        ],
        "example": "def smoothie_possible(self, nom):\n    return set(self.recettes[nom]).issubset(set(self.ingredients))",
    },
    "07": {
        "title": "Simulation d'une population de coccinelles",
        "focus": "Simulation jour par jour, objets, boucles d'arrêt et correction des règles biologiques.",
        "expected": [
            "Créer une population initiale et simuler cinq jours avec `evolution`.",
            "Automatiser jusqu'à 30 jours avec arrêt si coccinelles ou proies tombent à zéro.",
            "Documenter clairement la méthode `chasser`.",
            "Modifier la reproduction après maturité de 20 jours et la survie selon la nutrition.",
        ],
        "logic": [
            "Boucler sur les jours, appeler `evolution`, puis lire les tailles de population.",
            "Retourner `(nb_coccinelles, nb_pucerons, jours_simules)`.",
            "Dans les méthodes de classe, tester l'âge et la nutrition avant de reproduire ou survivre.",
        ],
        "example": "def simulation_simple(population, nb_proies):\n    jours = 0\n    while jours < 30 and population and nb_proies > 0:\n        population, nb_proies = evolution(population, nb_proies)\n        jours += 1\n    return len(population), nb_proies, jours",
    },
    "08": {
        "title": "Addition en BCD",
        "focus": "Limites des flottants, représentation BCD, conversion, correction de quartet et alignement.",
        "expected": [
            "Observer l'erreur de précision en additionnant des prix décimaux sous forme de flottants.",
            "Convertir une liste de quartets BCD en valeur décimale.",
            "Insérer l'étape de correction BCD après l'addition binaire de quartets.",
            "Aligner les nombres par ajout de quartets `0000` à gauche.",
        ],
        "logic": [
            "Lire chaque quartet comme un entier binaire avec `int(quartet, 2)`.",
            "Reconstruire la partie entière et les centimes selon la position.",
            "Après addition d'un quartet, si le résultat dépasse 9, ajouter 6 et propager la retenue.",
        ],
        "example": "def convertir_BCD_vers_decimal(qs):\n    chiffres = ''.join(str(int(q, 2)) for q in qs)\n    return int(chiffres[:-2]) + int(chiffres[-2:]) / 100",
    },
    "09": {
        "title": "Objets 3D et format OBJ",
        "focus": "Classes `Sommet`, `Face`, `Objet3D`, calculs géométriques et estimation d'impression 3D.",
        "expected": [
            "Compléter les méthodes d'ajout et d'affichage de sommets/faces.",
            "Calculer distances, arêtes adjacentes et volume du cube englobant.",
            "Utiliser les classes ensemble pour représenter un objet OBJ simple.",
            "Corriger l'estimation d'impression en s'appuyant sur les grandeurs demandées.",
        ],
        "logic": [
            "Les sommets sont identifiés par leur position dans une liste; les faces stockent des indices.",
            "Pour une face, les sommets adjacents sont les paires consécutives, avec retour au premier.",
            "Le cube englobant utilise les min/max de x, y et z.",
        ],
        "example": "def volume_cube_englobant(self):\n    xs = [s.x for s in self.sommets]\n    ys = [s.y for s in self.sommets]\n    zs = [s.z for s in self.sommets]\n    return (max(xs)-min(xs)) * (max(ys)-min(ys)) * (max(zs)-min(zs))",
    },
    "10": {
        "title": "Analyse de consommation d'eau",
        "focus": "Agrégation par jour, détection de fuite nocturne et lissage de série de mesures.",
        "expected": [
            "Calculer la consommation totale chaude + froide pour un jour donné, ou `None` sans mesure.",
            "Repérer au moins trois mesures nocturnes consécutives non nulles.",
            "Corriger `lissage_conso` pour renvoyer une liste de même taille.",
            "Traiter le cas limite d'une liste vide ou d'une seule valeur.",
        ],
        "logic": [
            "Filtrer les mesures dont la date correspond au jour.",
            "Parcourir les heures 00:00 à 05:00 en maintenant un compteur de mesures non nulles consécutives.",
            "Pour le lissage: premier/deuxième, dernier/avant-dernier, milieu avec trois valeurs.",
        ],
        "example": "def total_conso(donnees, jour):\n    totaux = [m['eau_chaude'] + m['eau_froide'] for m in donnees if m['jour'] == jour]\n    return None if not totaux else sum(totaux)",
    },
    "11": {
        "title": "Prédiction d'habitat par k plus proches voisins",
        "focus": "Distance euclidienne entre habitats, tri par distance et vote de présence.",
        "expected": [
            "Coder la distance sur vegetation, eau, densité urbaine et proies.",
            "Produire une liste de tuples `(distance, habitat)`.",
            "Corriger `presence_renard` pour lire correctement les tuples retournés.",
            "Tester plusieurs valeurs de `k` et justifier la conclusion.",
        ],
        "logic": [
            "Calculer la racine de la somme des carrés des écarts.",
            "Trier les tuples par première composante.",
            "Compter les `presence_renard` vrais parmi les `k` plus proches et décider à la majorité.",
        ],
        "example": "def distance(a, b):\n    champs = ['vegetation', 'proximite_eau', 'densite_urbaine', 'disponibilite_proies']\n    return sum((a[c] - b[c]) ** 2 for c in champs) ** 0.5",
    },
    "12": {
        "title": "Gestion d'un refuge de renards",
        "focus": "Classes, import CSV, conversion de types et analyse de corpulence.",
        "expected": [
            "Ecrire le constructeur et `__str__` de la classe `Renard`.",
            "Corriger l'import CSV pour convertir identifiant et poids dans les bons types.",
            "Instancier le refuge et importer les données réelles.",
            "Calculer et justifier le pourcentage de renards peu corpulents.",
        ],
        "logic": [
            "Stocker les attributs reçus sans transformer le format de date.",
            "Avec `csv.DictReader`, convertir `id` en `int` et `poids` en `float`.",
            "Filtrer les renards dont le poids est strictement inférieur au seuil.",
        ],
        "example": "class Renard:\n    def __init__(self, identifiant, nom, poids, date_arrivee):\n        self.id = identifiant\n        self.nom = nom\n        self.poids = poids\n        self.date_arrivee = date_arrivee",
    },
    "13": {
        "title": "Ballon-sonde et export KML",
        "focus": "Lecture CSV, conversion Kelvin/Celsius, recherche de minimum et écriture de fichier KML.",
        "expected": [
            "Récupérer altitudes, températures, longitudes et latitudes dans quatre listes.",
            "Convertir les températures de Kelvin vers Celsius avec arrondi à un chiffre.",
            "Trouver l'altitude associée à la température la plus froide.",
            "Ajouter une assertion de longueurs longitude/latitude et fermer la balise `</kml>`.",
        ],
        "logic": [
            "Utiliser la fonction fournie de lecture CSV pour produire les colonnes.",
            "Parcourir les indices ou utiliser `min` sur les températures.",
            "Avant génération KML, vérifier que les deux listes géographiques sont synchronisées.",
        ],
        "example": "def conversion_K_en_C(temperatures):\n    return [round(t - 273.15, 1) for t in temperatures]",
    },
    "14": {
        "title": "Simulation d'évacuation d'une pièce",
        "focus": "Grille, occupants, sorties, choix de sortie et simulation jusqu'à évacuation complète.",
        "expected": [
            "Compter les occupants restants dans la pièce.",
            "Simuler les tours jusqu'à évacuation complète avec option d'affichage.",
            "Ajouter les sorties Sud et Est sans modifier l'IHM.",
            "Corriger `choix_sortie` pour renvoyer la sortie la plus proche.",
        ],
        "logic": [
            "Parcourir la grille ou la structure des occupants et compter ceux qui ne sont pas sortis.",
            "Boucler tant que `nb_occupants_restants() > 0` et que `alerter` peut déplacer.",
            "Pour chaque occupant, calculer les distances aux sorties et garder le minimum.",
        ],
        "example": "def evacuation(piece, silencieux=True):\n    tours = 0\n    while piece.nb_occupants_restants() > 0 and piece.alerter():\n        tours += 1\n        if not silencieux:\n            print(piece)\n    return tours",
    },
    "15": {
        "title": "Cabinet vétérinaire et base SQLite",
        "focus": "Nettoyage de téléphone, validation, requêtes SQL et regroupement de consultations.",
        "expected": [
            "Normaliser un numéro en ne gardant que les chiffres.",
            "Ecrire des tests de validation de téléphone.",
            "Créer la requête des consultations de vaccination de chats depuis une date.",
            "Corriger `derniere_vaccination` pour conserver la date la plus récente par animal.",
        ],
        "logic": [
            "Pour le téléphone, filtrer les caractères avec `str.isdigit()`.",
            "Pour SQL, joindre les tables utiles et filtrer espèce, type de consultation et date.",
            "Parcourir les consultations et mettre à jour le dictionnaire si la date est plus récente.",
        ],
        "example": "def normalisation_tel(tel):\n    return ''.join(c for c in tel if c.isdigit())",
    },
    "16": {
        "title": "Warming stripes et prévision climatique",
        "focus": "Lecture de séries temporelles, recherche par année, régression simple et graphique.",
        "expected": [
            "Ecrire `ecart_temperature` qui renvoie l'écart d'une année ou `None`.",
            "Trouver la dernière année à écart négatif.",
            "Corriger `moyenne_ecarts` pour que la prévision 2040 redevienne cohérente.",
            "Générer les listes `annees` et `ordonnees` pour le graphique des warming stripes.",
        ],
        "logic": [
            "Parcourir les données, comparer l'année, retourner la valeur.",
            "Dans la moyenne, additionner les écarts et diviser par le nombre de valeurs, pas par une mauvaise grandeur.",
            "Pour le graphique, une ordonnée constante `1` donne des bandes de hauteur uniforme.",
        ],
        "example": "def ecart_temperature(datas, annee):\n    for ligne in datas:\n        if ligne['annee'] == annee:\n            return ligne['ecart']\n    return None",
    },
    "17": {
        "title": "Analyse de budget associatif",
        "focus": "CSV de mouvements, sommes par type, solde annuel et correction d'un filtre logique.",
        "expected": [
            "Sommer les montants dont le type vaut `dépense` ou `recette`.",
            "Tester le solde attendu sur `mouvements_test`.",
            "Corriger `solde_annuel` si elle ignore certains mouvements.",
            "Appliquer la fonction corrigée au fichier `budget_complet.csv`.",
        ],
        "logic": [
            "Lire le CSV en dictionnaires et convertir les montants en nombres.",
            "Le solde est total des recettes moins total des dépenses.",
            "Ne pas dépendre d'un mois ou d'une catégorie si le solde demandé est annuel.",
        ],
        "example": "def total_par_type(mouvements, type_mouvement):\n    return sum(m['montant'] for m in mouvements if m['type'] == type_mouvement)",
    },
    "18": {
        "title": "Températures en Polynésie",
        "focus": "Moyennes par zone, anomalies par seuil et évolution par décennie.",
        "expected": [
            "Calculer la température moyenne d'un archipel, ou `None` sans relevé.",
            "Renvoyer les dates dont l'écart absolu à la moyenne dépasse un seuil.",
            "Compléter trois tests ciblant `evolution_par_decennie`.",
            "Corriger le bug de regroupement ou de calcul par décennie.",
        ],
        "logic": [
            "Filtrer les relevés par zone.",
            "Calculer la moyenne puis reparcourir les relevés pour détecter les anomalies.",
            "Pour la décennie, calculer par exemple `(annee // 10) * 10` et agréger les températures.",
        ],
        "example": "def temperature_moyenne(zone, donnees):\n    valeurs = [d['temperature'] for d in donnees if d['zone'] == zone]\n    return None if not valeurs else sum(valeurs) / len(valeurs)",
    },
    "19": {
        "title": "Gestion de réservoirs d'eau",
        "focus": "Pénurie, volumes par district, moyenne globale et districts vulnérables.",
        "expected": [
            "Tester si un réservoir est sous 20 % de remplissage.",
            "Agrégater le volume disponible par district.",
            "Corriger `volume_moyen` avec tests sur liste vide et bornes de moyenne.",
            "Identifier les districts dont le volume moyen est inférieur à 80 % du global.",
        ],
        "logic": [
            "Calculer taux = volume disponible / capacité.",
            "Utiliser un dictionnaire accumulateur par district.",
            "Comparer la moyenne de chaque sous-liste de réservoirs à `0.8 * moyenne_globale`.",
        ],
        "example": "def volume_par_district(reservoirs):\n    volumes = {}\n    for r in reservoirs:\n        volumes[r['district']] = volumes.get(r['district'], 0) + r['volume']\n    return volumes",
    },
    "20": {
        "title": "Comparaison d'empreintes numériques",
        "focus": "Dictionnaires d'usages, calcul total, classement par impact et correction de comparaison.",
        "expected": [
            "Calculer l'empreinte totale mensuelle d'un utilisateur.",
            "Classer les activités en impacts fort, moyen et faible.",
            "Ajouter des tests pertinents à `comparer`.",
            "Identifier le cas d'erreur de pourcentage et corriger la division problématique.",
        ],
        "logic": [
            "Additionner les émissions de toutes les activités.",
            "Pour le classement, créer trois listes puis ajouter chaque activité selon ses seuils.",
            "Dans une comparaison relative, protéger le cas où la valeur de référence vaut zéro.",
        ],
        "example": "def classer_par_impact(utilisateur):\n    classes = {'fort': [], 'moyen': [], 'faible': []}\n    for nom, valeur in utilisateur.items():\n        classes['fort' if valeur >= 1000 else 'moyen' if valeur >= 200 else 'faible'].append(nom)\n    return classes",
    },
    "21": {
        "title": "Cartes mémoire et méthode de Leitner",
        "focus": "Objets carte, dates de révision, extraction du jour et renforcement des cartes faibles.",
        "expected": [
            "Mettre à jour le niveau d'une carte selon réussite ou échec.",
            "Calculer la prochaine date avec les délais de Leitner.",
            "Extraire les cartes dont la date de révision est atteinte.",
            "Corriger `extraire_cartes_a_renforcer` pour garder uniquement le niveau minimum exact.",
        ],
        "logic": [
            "Sur succès: augmenter le niveau sans dépasser la borne; sur échec: revenir au minimum.",
            "Utiliser `date_future(DELAIS[niveau])` pour planifier.",
            "Trouver le niveau minimum du paquet, puis filtrer `carte.niveau == minimum`.",
        ],
        "example": "def extraire_cartes_du_jour(paquet, date_jour):\n    return [carte for carte in paquet if carte.date_prochaine <= date_jour]",
    },
    "22": {
        "title": "QR code simplifié et ASCII",
        "focus": "Conversions binaire/décimal, décodage QR, table ASCII et encodage inverse.",
        "expected": [
            "Convertir un tuple binaire en entier.",
            "Transformer un QR code, liste de tuples, en liste de décimaux puis en chaîne.",
            "Corriger `dec2str` pour gérer les codes absents de la table ASCII.",
            "Corriger `str2qrcode` pour produire des tuples binaires avec la bonne largeur.",
        ],
        "logic": [
            "Accumuler les bits de gauche à droite: `valeur = valeur * 2 + bit`.",
            "Appliquer la conversion à chaque ligne du QR code.",
            "Pour l'encodage inverse, rechercher le code ASCII puis compléter à 8 bits si nécessaire.",
        ],
        "example": "def bin2dec(bits):\n    valeur = 0\n    for bit in bits:\n        valeur = valeur * 2 + bit\n    return valeur",
    },
    "23": {
        "title": "Décodage de transmissions environnementales",
        "focus": "Trames binaires, conversion de champs, parité de contrôle et robustesse du décodage.",
        "expected": [
            "Implémenter le décodage température et humidité dans la classe `Transmission`.",
            "Vérifier les quatre bits de contrôle par parité des blocs de données.",
            "Exécuter l'analyse des plus de 300 transmissions et repérer les erreurs possibles.",
            "Rendre la classe robuste face aux longueurs invalides, bits non binaires et valeurs incohérentes.",
        ],
        "logic": [
            "Découper la chaîne: ID 0:8, clé 8:16, température 16:28, humidité 28:36, contrôle 36:40.",
            "Convertir un champ binaire avec `int(champ, 2)`.",
            "Température = `(valeur - 900) / 10`; humidité = valeur décimale.",
            "Comparer chaque bit de contrôle à la parité calculée du bloc correspondant.",
        ],
        "example": "def decoder_temperature(self):\n    valeur = int(self.trame[16:28], 2)\n    self.temperature = (valeur - 900) / 10",
    },
}


def register_fonts() -> tuple[str, str]:
    candidates = [
        Path(r"C:\Windows\Fonts\arial.ttf"),
        Path(r"C:\Windows\Fonts\segoeui.ttf"),
    ]
    mono_candidates = [
        Path(r"C:\Windows\Fonts\consola.ttf"),
        Path(r"C:\Windows\Fonts\cour.ttf"),
    ]
    font = "Helvetica"
    mono = "Courier"
    for candidate in candidates:
        if candidate.exists():
            pdfmetrics.registerFont(TTFont("LocalSans", str(candidate)))
            font = "LocalSans"
            break
    for candidate in mono_candidates:
        if candidate.exists():
            pdfmetrics.registerFont(TTFont("LocalMono", str(candidate)))
            mono = "LocalMono"
            break
    return font, mono


def extract_pdf_text(pdf_path: Path) -> str:
    with fitz.open(pdf_path) as doc:
        return "\n".join(page.get_text() for page in doc)


def extract_questions(text: str) -> list[str]:
    chunks = re.findall(
        r"Question\s+\d+\s*(.*?)(?=Question\s+\d+|Description du dossier|Préparation de l’environnement|$)",
        text,
        flags=re.S,
    )
    result = []
    for chunk in chunks:
        cleaned = re.sub(r"\s+", " ", chunk).strip()
        cleaned = cleaned.replace("Hand-paper", "").strip()
        if len(cleaned) > 260:
            cleaned = cleaned[:257].rsplit(" ", 1)[0] + "..."
        result.append(cleaned)
    return result


def inspect_python_files(folder: Path) -> tuple[list[str], list[str]]:
    functions: set[str] = set()
    classes: set[str] = set()
    for path in sorted(folder.glob("*.py")):
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"))
        except UnicodeDecodeError:
            tree = ast.parse(path.read_text(encoding="cp1252"))
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef):
                functions.add(node.name)
            elif isinstance(node, ast.ClassDef):
                classes.add(node.name)
    return sorted(functions), sorted(classes)


def source_files(folder: Path) -> list[str]:
    return sorted(
        path.name
        for path in folder.iterdir()
        if path.is_file() and path.name.lower() != "sujet.pdf"
    )


def bullet_items(items: list[str], style: ParagraphStyle) -> list[Paragraph]:
    return [
        Paragraph(f"• {html.escape(item)}", style)
        for item in items
    ]


def code_block(text: str, style: ParagraphStyle) -> Preformatted:
    return Preformatted(text, style)


def page_footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(colors.HexColor("#666666"))
    canvas.drawString(1.6 * cm, 1.0 * cm, f"Generated by {GENERATOR_NAME}")
    canvas.drawRightString(19.4 * cm, 1.0 * cm, f"Page {doc.page}")
    canvas.restoreState()


def build_pdf(subject_id: str, source_folder: Path, output_pdf: Path, output_md: Path) -> dict[str, object]:
    info = SUBJECTS[subject_id]
    text = extract_pdf_text(source_folder / "sujet.pdf")
    questions = extract_questions(text)
    functions, classes = inspect_python_files(source_folder)
    files = source_files(source_folder)

    font, mono = register_fonts()
    styles = getSampleStyleSheet()
    styles.add(
        ParagraphStyle(
            name="HesiaTitle",
            parent=styles["Title"],
            fontName=font,
            fontSize=17,
            leading=21,
            textColor=colors.HexColor("#1f2937"),
            spaceAfter=8,
            alignment=TA_LEFT,
        )
    )
    styles.add(
        ParagraphStyle(
            name="HesiaH2",
            parent=styles["Heading2"],
            fontName=font,
            fontSize=11,
            leading=14,
            textColor=colors.HexColor("#0f766e"),
            spaceBefore=8,
            spaceAfter=4,
        )
    )
    styles.add(
        ParagraphStyle(
            name="HesiaBody",
            parent=styles["BodyText"],
            fontName=font,
            fontSize=9.4,
            leading=12.4,
            spaceAfter=3,
        )
    )
    styles.add(
        ParagraphStyle(
            name="HesiaSmall",
            parent=styles["BodyText"],
            fontName=font,
            fontSize=8,
            leading=10,
            textColor=colors.HexColor("#4b5563"),
        )
    )
    styles.add(
        ParagraphStyle(
            name="HesiaCode",
            parent=styles["Code"],
            fontName=mono,
            fontSize=7.8,
            leading=9.5,
            borderPadding=6,
            backColor=colors.HexColor("#f3f4f6"),
            textColor=colors.HexColor("#111827"),
        )
    )

    doc = BaseDocTemplate(
        str(output_pdf),
        pagesize=A4,
        leftMargin=1.6 * cm,
        rightMargin=1.6 * cm,
        topMargin=1.5 * cm,
        bottomMargin=1.6 * cm,
        title=f"ECE NSI Sujet {subject_id} - {info['title']}",
    )
    frame = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="normal")
    doc.addPageTemplates([PageTemplate(id="main", frames=[frame], onPage=page_footer)])

    story = [
        Paragraph(f"Sujet {subject_id} - {html.escape(str(info['title']))}", styles["HesiaTitle"]),
        Paragraph(html.escape(str(info["focus"])), styles["HesiaBody"]),
        HRFlowable(width="100%", thickness=0.7, color=colors.HexColor("#d1d5db"), spaceBefore=4, spaceAfter=6),
        Paragraph("Sur quoi cela porte", styles["HesiaH2"]),
        Paragraph(
            "Cette fiche résume le sujet, les réponses à préparer et le script à produire. "
            "Elle s'appuie sur le PDF et les fichiers sources présents dans le dossier.",
            styles["HesiaBody"],
        ),
        Paragraph("Réponses attendues", styles["HesiaH2"]),
        *bullet_items(list(info["expected"]), styles["HesiaBody"]),
        Paragraph("Logique du script", styles["HesiaH2"]),
        *bullet_items(list(info["logic"]), styles["HesiaBody"]),
        Paragraph("Exemple minimal", styles["HesiaH2"]),
        code_block(str(info["example"]), styles["HesiaCode"]),
        Paragraph("Questions repérées dans le sujet", styles["HesiaH2"]),
    ]
    for index, question in enumerate(questions, 1):
        story.append(Paragraph(f"{index}. {html.escape(question)}", styles["HesiaSmall"]))

    evidence = []
    if files:
        evidence.append("Fichiers: " + ", ".join(files))
    if classes:
        evidence.append("Classes: " + ", ".join(classes))
    if functions:
        evidence.append("Fonctions: " + ", ".join(functions))
    story.extend([
        Paragraph("Preuves locales", styles["HesiaH2"]),
        *[Paragraph(html.escape(item), styles["HesiaSmall"]) for item in evidence],
    ])

    doc.build(story)

    md_lines = [
        f"# Sujet {subject_id} - {info['title']}",
        "",
        f"## Sur quoi cela porte",
        str(info["focus"]),
        "",
        "## Réponses attendues",
        *[f"- {item}" for item in info["expected"]],
        "",
        "## Logique du script",
        *[f"- {item}" for item in info["logic"]],
        "",
        "## Exemple minimal",
        "```python",
        str(info["example"]),
        "```",
        "",
        "## Questions repérées",
        *[f"{i}. {q}" for i, q in enumerate(questions, 1)],
        "",
        "## Preuves locales",
        *[f"- {item}" for item in evidence],
        "",
    ]
    output_md.write_text("\n".join(md_lines), encoding="utf-8")

    return {
        "subject": subject_id,
        "pdf": str(output_pdf),
        "markdown": str(output_md),
        "source_files": files,
        "functions": functions,
        "classes": classes,
        "questions": len(questions),
    }


def main(argv: list[str]) -> int:
    source = Path(argv[1]) if len(argv) > 1 else DEFAULT_SOURCE
    output = Path(argv[2]) if len(argv) > 2 else DEFAULT_OUTPUT
    output.mkdir(parents=True, exist_ok=True)
    (output / "markdown").mkdir(exist_ok=True)

    if not source.exists():
        raise SystemExit(f"Source folder not found: {source}")

    manifest: list[dict[str, object]] = []
    for subject_id in sorted(SUBJECTS):
        folder = source / subject_id
        if not (folder / "sujet.pdf").exists():
            raise SystemExit(f"Missing sujet.pdf for subject {subject_id}: {folder}")
        manifest.append(
            build_pdf(
                subject_id,
                folder,
                output / f"ECE_NSI_{subject_id}_recap.pdf",
                output / "markdown" / f"ECE_NSI_{subject_id}_recap.md",
            )
        )

    manifest_lines = [
        "# Epsilon NSI generation manifest",
        "",
        f"Source folder: {source}",
        f"Output folder: {output}",
        f"Subjects generated: {len(manifest)}",
        "",
    ]
    for item in manifest:
        manifest_lines.append(
            f"- Sujet {item['subject']}: {Path(str(item['pdf'])).name} "
            f"({item['questions']} questions, {len(item['source_files'])} source files)"
        )
    (output / "MANIFEST.md").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")
    print(f"Generated {len(manifest)} PDFs in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
