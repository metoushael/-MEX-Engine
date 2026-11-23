---
nom: conception
description: Transformer une idée de jeu en plan réalisable (périmètre, scènes, étapes jouables). À charger quand l'utilisateur décrit un jeu entier ou demande « fais-moi un jeu ».
---
# De l'idée au jeu

## Avant d'écrire du code

1. Regarde le projet : `lister_fichiers`, `lire_fichier("res://project.mex")`. Un projet déjà
   commencé se prolonge ; on ne repart pas de zéro sans le demander.
2. Résume le jeu en trois lignes : ce que fait le joueur, ce qui le fait gagner, ce qui le
   fait perdre. Si l'un des trois manque et que la demande ne permet pas de le deviner, pose
   UNE question courte ; sinon choisis le plus simple et annonce ton choix.
3. Annonce un plan en étapes dont chacune donne un jeu qui se lance.

## Viser petit d'abord

Le premier objectif est toujours une version jouable en une scène : un personnage qui
bouge, un but, une fin. Menu, sons, niveaux supplémentaires et effets viennent APRÈS, un par
un. Un jeu modeste qui fonctionne vaut mieux qu'un grand projet qui ne démarre pas.

## Organisation des fichiers

```
res://project.mex            paramètres (jamais écrit à la main : outil definir_parametre)
res://principal.scn          scène de départ (application/main_scene)
res://joueur.scn, joueur.mx  une scène et un script par objet réutilisable
res://niveaux/niveau1.scn
res://images/, res://sons/
```

Noms de fichiers en minuscules, sans espace ni accent.

## Images et sons

Tu ne peux pas dessiner ni composer. Pour avancer : `creer_image` fabrique des formes unies
(un carré bleu pour le joueur, un disque jaune pour une pièce), et les `ColorRect` font de
bons décors provisoires. Dis à l'utilisateur quels fichiers il pourra remplacer par ses
propres images, et à quelle taille.

## Travailler par étapes

Pour chaque étape : écrire, vérifier (`ecrire_fichier` signale les erreurs de syntaxe),
`lancer_jeu`, lire la sortie, corriger. Ne passe pas à l'étape suivante avec une erreur dans
la sortie.

## Rendre compte

À la fin, dis en quelques lignes : ce qui a été créé (fichiers), comment y jouer (touches),
ce que tu as vérifié en lançant le jeu, ce que tu n'as PAS pu vérifier (tout ce qui se
voit ou se ressent), et la prochaine étape que tu proposes.
