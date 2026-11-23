---
nom: interface
description: Menus et interface de jeu (Control, conteneurs, boutons, score à l'écran, navigation au clavier et à la manette). À charger pour un menu, un écran de pause, un affichage de score.
---
# Interface de jeu

## Menu principal

```
[mex_scene format=1]

[ext_resource type="MexScript" path="res://menu.mx" id="1"]

[node name="Menu" type="Control"]
anchor_right = 1.0
anchor_bottom = 1.0
offset_right = 0.0
offset_bottom = 0.0
script = ExtResource("1")

[node name="Fond" type="ColorRect" parent="."]
anchor_right = 1.0
anchor_bottom = 1.0
offset_right = 0.0
offset_bottom = 0.0
color = Color(0.16, 0.2, 0.33, 1)

[node name="Centre" type="CenterContainer" parent="."]
anchor_right = 1.0
anchor_bottom = 1.0
offset_right = 0.0
offset_bottom = 0.0

[node name="Boite" type="VBoxContainer" parent="Centre"]
separation = 14

[node name="Titre" type="Label" parent="Centre/Boite"]
text = "Mon jeu"
font_size = 56
horizontal_alignment = 1

[node name="Jouer" type="Button" parent="Centre/Boite"]
custom_minimum_size = Vector2(300, 46)
text = "Jouer"

[node name="Quitter" type="Button" parent="Centre/Boite"]
custom_minimum_size = Vector2(300, 46)
text = "Quitter"

[connection signal="pressed" from="Centre/Boite/Jouer" to="." method="_sur_jouer"]
[connection signal="pressed" from="Centre/Boite/Quitter" to="." method="_sur_quitter"]
```

```gdscript
extends Control

func _sur_jouer():
	get_tree().change_scene_to_file("res://niveau.scn")

func _sur_quitter():
	get_tree().quit()
```

## Principes

- Les conteneurs placent leurs enfants : `VBoxContainer`, `HBoxContainer`, `GridContainer`,
  `MarginContainer`, `CenterContainer`, `ScrollContainer`, `TabContainer`. Dans un
  conteneur, on ne fixe pas la position d'un enfant : on règle `custom_minimum_size`.
- Hors conteneur, un `Control` se place avec ses ancres (`anchor_left/top/right/bottom`,
  de 0 à 1) et ses décalages (`offset_*`, en pixels).
- Affichage par-dessus le jeu (score, vies) : un `CanvasLayer` qui contient les `Label`.
  Il ne bouge pas avec la caméra.
- Autres contrôles : `CheckBox`, `OptionButton`, `LineEdit`, `HSlider`, `ProgressBar`,
  `TextureRect`, `TextureButton`, `Panel`, `ItemList`.
- Clavier et manette : les flèches ou la croix déplacent le focus entre les boutons,
  Entrée ou le bouton A valide. Rien à programmer.
- Sur écran tactile, un toucher agit comme un clic : les boutons fonctionnent au doigt.

Les noms exacts des propriétés et des signaux : `documentation_classe`.
