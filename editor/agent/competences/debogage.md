---
nom: debogage
description: Méthode pour trouver et corriger une erreur (lire la sortie du jeu, isoler la cause, vérifier le correctif). À charger quand le jeu ne démarre pas, plante ou ne fait pas ce qui est attendu.
---
# Trouver et corriger une erreur

## Méthode

1. **Reproduire.** `lancer_jeu` (quelques secondes suffisent) et lis TOUTE la sortie. La
   première erreur est la cause ; les suivantes en sont souvent les conséquences.
2. **Lire le fichier fautif** avec `lire_fichier` avant de le changer. Ne corrige jamais de
   mémoire un fichier que tu n'as pas relu : l'utilisateur a pu le modifier.
3. **Une hypothèse, un changement.** Corrige la cause, pas le symptôme, avec
   `remplacer_dans_fichier` (petit changement) plutôt qu'en réécrivant tout le fichier.
4. **Vérifier.** Relance le jeu. Dis ce que tu as observé, pas ce que tu espères.

## Ce que l'outil peut et ne peut pas voir

`lancer_jeu` renvoie le texte de la sortie : erreurs, avertissements, `print`. Il ne VOIT
PAS l'image et ne joue pas : il ne peut pas confirmer qu'un personnage saute bien ou qu'une
couleur est jolie. Pour vérifier un comportement, ajoute des `print` provisoires (position,
état, passage dans une fonction), lis la sortie, puis retire-les. Quand seul l'utilisateur
peut juger, demande-lui d'essayer et dis précisément quoi regarder.

## Erreurs fréquentes

| Message ou symptôme | Cause habituelle |
| --- | --- |
| Erreur de syntaxe avec un numéro de ligne | Construction GDScript absente de MexScript (`await`, `**`, lambda sur plusieurs lignes…) : compétence `mexscript` |
| Identifiant non déclaré | Variable mal orthographiée, ou classe / fonction de Godot qui n'existe pas dans MEX : `documentation_classe` |
| Nœud introuvable avec `$Nom` | Le chemin ne correspond pas à la scène : `scene_ouverte` ou `lire_fichier` sur le `.scn` |
| Le personnage traverse le sol | `CollisionShape2D` absente ou sans `shape` |
| Rien ne bouge | Action d'entrée non déclarée, ou script non attaché au nœud (`script = ExtResource(...)`) |
| La scène ne s'ouvre pas | Parent écrit après son enfant, `id` de ressource inconnu, première ligne absente : compétence `scenes` |
| Écran vide au lancement | `application/main_scene` vide ou fichier absent |

## Quand tu es bloqué

Dis-le. Résume ce que tu as essayé, ce que montre la sortie, et ce que tu ne peux pas
vérifier toi-même. N'annonce pas qu'un problème est réglé si le jeu n'a pas été relancé.
