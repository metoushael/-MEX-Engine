---
nom: vue-de-dessus
description: Recette d'un jeu 2D vu de dessus (déplacement dans les quatre directions, murs, ennemis qui suivent, objets). À charger pour un jeu d'aventure, de tir ou de labyrinthe.
---
# Jeu 2D vu de dessus

## Déplacement

```gdscript
extends CharacterBody2D

@export var vitesse: float = 220.0

func _physics_process(delta):
	var direction = Input.get_vector("gauche", "droite", "haut", "bas")
	velocity = direction * vitesse
	move_and_slide()
```

`Input.get_vector` renvoie un vecteur de longueur 1 au plus : la diagonale n'est pas plus
rapide, et le stick d'une manette ou le `VirtualJoystick` tactile donnent une vitesse
progressive. Pas de gravité dans ce genre de jeu : ne l'ajoute pas à `velocity`.

Actions : `gauche`, `droite`, `haut`, `bas` (compétence `controles`).

## Murs et décor

`StaticBody2D` + `CollisionShape2D`. Pour un grand niveau en cases, regarde `TileMap` avec
`documentation_classe`.

## Ennemi qui suit le joueur

```gdscript
extends CharacterBody2D

@export var vitesse: float = 120.0
var cible = null

func _ready():
	var joueurs = get_tree().get_nodes_in_group("joueur")
	if joueurs.size() > 0:
		cible = joueurs[0]

func _physics_process(delta):
	if cible == null:
		return
	var vers = cible.global_position - global_position
	if vers.length() > 4:
		velocity = vers.normalized() * vitesse
		move_and_slide()
```

Pour contourner les murs, MEX fournit `AStarGrid2D` et `AStar2D` (voir leur documentation).

## Zones : objets, dégâts, sorties

Un `Area2D` avec sa `CollisionShape2D` et le signal `body_entered` (voir la pièce de la
compétence `plateforme-2d`).

## Minuterie sans `await`

MexScript n'a pas `await`. Pour « attendre 2 secondes » : un nœud `Timer` dont le signal
`timeout` est connecté à une fonction, ou un compteur :

```gdscript
var attente = 0.0

func _process(delta):
	attente -= delta
	if attente <= 0:
		attente = 2.0
		apparaitre_ennemi()
```
