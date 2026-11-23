---
nom: controles
description: Actions d'entrée du projet, clavier, souris, manette et commandes tactiles (TouchScreenButton, VirtualJoystick). À charger avant de déclarer des actions ou d'ajouter des commandes à l'écran.
---
# Contrôles

## Actions

Un jeu lit des ACTIONS, pas des touches. Elles vivent dans les paramètres du projet, section
`input`. Avec l'outil `definir_parametre` :

```
definir_parametre(nom = "input/sauter", valeur = ["Space", "W", "JoyA"])
```

Noms de touches : lettres `A`…`Z`, chiffres, `Space`, `Enter`, `Escape`, `Tab`, `Shift`,
`Ctrl`, `Left`, `Right`, `Up`, `Down`.
Manette : `JoyA`, `JoyB`, `JoyX`, `JoyY`, `JoyBack`, `JoyStart`, `JoyDpadUp`, `JoyDpadDown`,
`JoyDpadLeft`, `JoyDpadRight`, `JoyLeftX-`, `JoyLeftX+`, `JoyLeftY-`, `JoyLeftY+`,
`JoyRightX-`, `JoyRightX+`, `JoyRightY-`, `JoyRightY+`, `JoyTriggerLeft`, `JoyTriggerRight`.

Les actions `ui_left`, `ui_right`, `ui_up`, `ui_down`, `ui_accept`, `ui_cancel` existent
déjà (menus). Regarde les actions du projet avec `lire_fichier("res://project.mex")` avant
d'en créer.

## Lire les actions en script

```gdscript
Input.is_action_pressed("sauter")          # tant que c'est enfoncé
Input.is_action_just_pressed("sauter")     # seulement l'image où ça s'enfonce
Input.get_axis("gauche", "droite")         # de -1 à 1
Input.get_vector("gauche", "droite", "haut", "bas")
Input.get_action_strength("accelerer")     # de 0 à 1 (gâchette, stick)
```

## Commandes tactiles (téléphone, tablette)

Deux nœuds, à placer dans un `CanvasLayer` pour qu'ils restent fixes à l'écran :

```
[node name="Tactile" type="CanvasLayer" parent="."]

[node name="Stick" type="VirtualJoystick" parent="Tactile"]
position = Vector2(130, 430)
radius = 72.0
action_left = "gauche"
action_right = "droite"
action_up = "haut"
action_down = "bas"
visibility_mode = 1

[node name="Saut" type="TouchScreenButton" parent="Tactile"]
position = Vector2(780, 380)
radius = 48.0
action = "sauter"
visibility_mode = 1
```

- `TouchScreenButton` : presse l'action `action` tant qu'un doigt est dessus. Sans texture,
  il dessine un disque de rayon `radius` ; sa position est son coin supérieur gauche. Avec
  `texture_normal` / `texture_pressed`, il dessine ces images. `passby_press = true` le
  déclenche aussi quand le doigt glisse dessus. Signaux `pressed` et `released`.
- `VirtualJoystick` : stick à l'écran, sa position est son CENTRE. Il alimente quatre
  actions comme le stick d'une manette (`Input.get_vector` fonctionne tel quel).
- `visibility_mode = 1` : visible seulement sur un appareil tactile. `0` : toujours visible.
- Plusieurs doigts à la fois sont gérés (courir et sauter).
- Le script du jeu ne change pas : il continue de lire les actions.
- Pour essayer à la souris sur un ordinateur :
  `definir_parametre("input_devices/pointing/emulate_touch_from_mouse", true)`.
- Événements bruts dans `_input(event)` : `InputEventScreenTouch` (`index`, `position`,
  `pressed`) et `InputEventScreenDrag` (`index`, `position`, `relative`).
