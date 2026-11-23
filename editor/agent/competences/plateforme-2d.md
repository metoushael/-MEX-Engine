---
nom: plateforme-2d
description: Recette d'un jeu de plateforme 2D (joueur qui court et saute, sol, pièces, caméra). À charger pour créer ou étendre un jeu de ce genre.
---
# Jeu de plateforme 2D

Le projet d'exemple `projects/plateforme` de MEX est construit ainsi.

## Nœuds

```
Niveau (Node2D)
├── Sol (StaticBody2D)            + CollisionShape2D (RectangleShape2D) + image
├── Joueur (CharacterBody2D)      script joueur.mx, groupe "joueur"
│   ├── Sprite (Sprite2D ou AnimatedSprite2D)
│   ├── Forme (CollisionShape2D)
│   └── Camera2D                  suit le joueur ; limit_left / limit_right / limit_bottom
├── Pieces (Node2D)               instances de piece.scn (Area2D, groupe "pieces")
└── Interface (CanvasLayer)       Label du score : reste fixe à l'écran
```

## Actions à déclarer (compétence `controles`)

```
gauche  = ["Left", "A", "JoyDpadLeft", "JoyLeftX-"]
droite  = ["Right", "D", "JoyDpadRight", "JoyLeftX+"]
sauter  = ["Space", "Up", "W", "JoyA"]
```

## Script du joueur

```gdscript
extends CharacterBody2D

@export var vitesse: float = 260.0
@export var force_saut: float = 560.0

var gravite = 980.0

func _physics_process(delta):
	var direction = Input.get_axis("gauche", "droite")
	velocity.x = direction * vitesse
	if not is_on_floor():
		velocity.y += gravite * delta
	elif Input.is_action_just_pressed("sauter"):
		velocity.y = -force_saut
	move_and_slide()
```

L'axe Y pointe vers le BAS : sauter, c'est une vitesse négative. La gravité du projet est le
paramètre `physics/gravity` (980 par défaut, en pixels/s²).

## Pièce à ramasser

```gdscript
extends Area2D

signal ramassee

func _on_body_entered(corps):
	if corps.is_in_group("joueur"):
		ramassee.emit()
		queue_free()
```

avec, dans `piece.scn` : `[connection signal="body_entered" from="." to="." method="_on_body_entered"]`.

## Compter les pièces dans le niveau

```gdscript
var score = 0
var total = 0

func _ready():
	var pieces = get_tree().get_nodes_in_group("pieces")
	total = pieces.size()
	for piece in pieces:
		piece.ramassee.connect(_sur_piece_ramassee)

func _sur_piece_ramassee():
	score += 1
	$Interface/Score.text = "%d / %d" % [score, total]
```

## Ordre de travail conseillé

1. Actions, puis une scène avec le sol et le joueur ; `lancer_jeu` pour vérifier qu'il tient
   sur le sol.
2. Saut, caméra, limites.
3. Pièces et score, puis menu, puis fin de partie (`reload_current_scene`).
Ajoute une chose à la fois et relance le jeu entre deux étapes.
