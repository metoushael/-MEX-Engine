---
nom: scenes
description: Format exact des fichiers de scène .scn (nœuds, ressources, instances, groupes, connexions de signaux). À charger avant de créer ou de modifier une scène.
---
# Fichiers de scène `.scn`

Une scène est un fichier texte. Tu la crées ou la modifies avec `ecrire_fichier` ou
`remplacer_dans_fichier`, puis `ouvrir_scene` la montre à l'utilisateur. `ecrire_fichier`
recharge la scène et signale si elle est invalide.

## Exemple complet

```
[mex_scene format=1]

[ext_resource type="MexScript" path="res://joueur.mx" id="1"]
[ext_resource type="Texture2D" path="res://joueur.png" id="2"]
[ext_resource type="PackedScene" path="res://piece.scn" id="3"]

[sub_resource type="RectangleShape2D" id="1"]
size = Vector2(20, 30)

[node name="Niveau" type="Node2D"]

[node name="Joueur" type="CharacterBody2D" parent="." groups=["joueur"]]
position = Vector2(120, 300)
script = ExtResource("1")

[node name="Sprite" type="Sprite2D" parent="Joueur"]
texture = ExtResource("2")

[node name="Forme" type="CollisionShape2D" parent="Joueur"]
shape = SubResource("1")

[node name="Camera2D" type="Camera2D" parent="Joueur"]

[node name="Piece1" parent="." instance=ExtResource("3")]
position = Vector2(240, 336)

[connection signal="body_entered" from="Piece1" to="." method="_sur_piece"]
```

## Règles

- Première ligne : `[mex_scene format=1]`.
- Ordre : `ext_resource`, puis `sub_resource`, puis les nœuds, puis les `connection`.
- Le PREMIER nœud est la racine : pas de `parent`. Ses enfants directs ont `parent="."`,
  les suivants le chemin depuis la racine sans son nom : `parent="Joueur"`,
  `parent="Interface/Boite"`. Un parent doit être écrit AVANT ses enfants.
- Les identifiants `id` des `ext_resource` et ceux des `sub_resource` sont deux séries
  séparées ; chacun est unique dans sa série.
- Une scène placée dans une autre : `instance=ExtResource("id")` à la place de `type=`.
- Groupes : `groups=["ennemis", "pieces"]` dans l'en-tête du nœud.
- Seules les propriétés différentes de la valeur par défaut sont écrites. Leurs noms exacts
  viennent de `documentation_classe` : ne devine pas.
- Valeurs : `12`, `1.5`, `true`, `"texte"`, `Vector2(x, y)`, `Vector3(x, y, z)`,
  `Color(r, v, b, a)` entre 0 et 1, `Rect2(x, y, l, h)`, `["a", "b"]`.
- Connexion : `from` et `to` sont des chemins depuis la racine (`"."` pour la racine) ;
  `method` est une fonction du script du nœud `to`.

## Pièges fréquents

- Un corps physique (`CharacterBody2D`, `StaticBody2D`, `RigidBody2D`, `Area2D`) sans
  enfant `CollisionShape2D` muni d'une `shape` ne touche rien.
- Un `Control` qui doit remplir l'écran : `anchor_right = 1.0`, `anchor_bottom = 1.0`,
  `offset_right = 0.0`, `offset_bottom = 0.0`.
- Pas d'image disponible ? `creer_image` fabrique un PNG uni (rectangle ou disque) qui sert
  de sprite provisoire.
- La scène de départ du jeu est le paramètre `application/main_scene`
  (`definir_parametre`).
