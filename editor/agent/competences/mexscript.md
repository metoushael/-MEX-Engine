---
nom: mexscript
description: Syntaxe exacte de MexScript (fichiers .mx) et différences avec GDScript. À charger avant d'écrire ou de corriger un script.
---
# MexScript

MexScript ressemble à GDScript, mais ce n'est PAS GDScript : seule la liste ci-dessous existe.
En cas de doute sur une classe, une méthode ou une propriété, appelle `documentation_classe`
au lieu de supposer que c'est comme dans Godot.

## Forme d'un script

```gdscript
extends CharacterBody2D
# Commentaire. L'indentation se fait avec des TABULATIONS.

signal touche(degats)
enum Etat { REPOS, COURSE, SAUT }
const VITESSE_MAX = 400.0

@export var vitesse: float = 260.0      # visible dans l'inspecteur, enregistré dans la scène
@onready var sprite = $Sprite           # résolu juste avant _ready
var points = 0

func _ready():
	print("prêt : ", name)

func _physics_process(delta):
	var direction = Input.get_axis("gauche", "droite")
	velocity.x = direction * vitesse
	move_and_slide()

func ajouter(valeur: int = 1) -> int:
	points += valeur
	return points
```

## Pris en charge

- `var`, `const`, `signal`, `enum`, `func`, `static func`, `@export`, `@onready`.
- `if / elif / else`, `while`, `for x in tableau`, `for i in range(n)`, `match`, `break`,
  `continue`, `return`, `pass`.
- Opérateur ternaire `a if condition else b`, `and`, `or`, `not`, `in`, `is`.
- Lambdas SUR UNE LIGNE : `bouton.pressed.connect(func(): print("clic"))`.
- `$Chemin/Du/Noeud`, `get_node("Chemin")`, annotations de type, paramètres par défaut.
- Chaînes avec `%` : `"%s : %d / %d" % [nom, score, total]`, et `str(valeur)`.
- Tableaux `[1, 2]`, dictionnaires `{"cle": valeur}`, `Vector2`, `Vector3`, `Color`, `Rect2`.

## PAS pris en charge (ne jamais écrire)

- `await` (utilise un nœud `Timer` et son signal `timeout`, ou un compteur dans `_process`).
- `extends "res://chemin.mx"` (seulement `extends NomDeClasse` du moteur).
- Classes internes `class Nom:`.
- Accesseurs `set` / `get` sur une variable.
- `%NomUnique`.
- Opérateur `**` (utilise `pow(a, b)`).
- Lambdas sur plusieurs lignes : écris une fonction nommée et passe son nom.

## Fonctions appelées par le moteur

`_init`, `_ready`, `_process(delta)`, `_physics_process(delta)`, `_input(event)`,
`_unhandled_input(event)`, `_draw`.

## Gestes courants (vérifiés dans les projets d'exemple)

```gdscript
get_tree().change_scene_to_file("res://menu.scn")
get_tree().reload_current_scene()
get_tree().quit()
for piece in get_tree().get_nodes_in_group("pieces"):
	piece.ramassee.connect(_sur_piece_ramassee)     # signal -> fonction nommée
ramassee.emit()                                       # émettre son propre signal
queue_free()                                          # supprimer le nœud
if corps.is_in_group("joueur"): ...
var scene = load("res://piece.scn")
var copie = scene.instantiate()
add_child(copie)
etiquette.text = "Score : %d" % score
```

## Données gardées d'une scène à l'autre, et JSON

- Chargement automatique : un script (ou une scène) déclaré dans la section `[autoload]` de
  `project.mex` est créé avant la première scène et survit aux changements de scène. Tous
  les scripts l'appellent par son nom : `Partie.score += 1`. Pour le déclarer, appelle
  `definir_parametre` avec le nom `autoload/Partie` et la valeur `"res://partie.mx"`. Le
  nom ne doit pas être celui d'une classe du moteur (`Input`, `Node`...).
- `JSON.stringify(valeur, "\t")` donne un texte ; `JSON.parse_string(texte)` rend la
  valeur, ou `null` si le texte est invalide.

## Entrées

`Input.is_action_pressed("a")`, `Input.is_action_just_pressed("a")`,
`Input.is_action_just_released("a")`, `Input.get_axis("gauche", "droite")`,
`Input.get_vector("gauche", "droite", "haut", "bas")`, `Input.get_action_strength("a")`.
Les actions sont déclarées dans les paramètres du projet (compétence `controles`).

## Après avoir écrit un script

`ecrire_fichier` vérifie déjà la syntaxe et renvoie l'erreur avec sa ligne. Les erreurs
d'exécution (méthode inconnue, nœud introuvable) n'apparaissent qu'avec `lancer_jeu`.
