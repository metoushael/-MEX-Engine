# Mex — Guide d'usage du logo

## 1. Le logo
- **Idée** : le haut de la manette dessine un M. Le nom et l'objet tiennent dans un seul signe.
- **Versions** : horizontale (principale) · empilée · symbole seul · wordmark seul.
- **Choisir le bon fichier** : `svg/` pour tout usage (web, impression, découpe) · `png/` pour les documents et réseaux · `web/` pour le site, favicon et PWA.

## 2. Couleurs
| Nom | HEX | RGB | CMJN (approx.) | Usage |
|---|---|---|---|---|
| Vert Mex | #19D46A | 25 · 212 · 106 | 88 · 0 · 50 · 17 | symbole sur fond sombre |
| Vert profond | #087A3A | 8 · 122 · 58 | 93 · 0 · 52 · 52 | symbole sur fond clair |
| Noir Mex | #0C110E | 12 · 17 · 14 | 20 · 0 · 18 · 95 | fond sombre, texte, wordmark clair |
| Blanc cassé | #F4F7F5 | 244 · 247 · 245 | 2 · 0 · 1 · 3 | wordmark sur fond sombre |

Le CMJN est une conversion indicative : faites valider les couleurs par l'imprimeur (épreuve ou Pantone équivalent).

**Associations approuvées**
- Sur fond clair (blanc, blanc cassé) : `*-light` (symbole vert profond + MEX noir) ou `*-black`.
- Sur fond sombre (noir Mex) : `*-dark` (symbole vert vif + MEX blanc cassé) ou `*-white`.
- Sur un vert ou une photo : `*-black` ou `*-white`, sur une zone calme.

**À ne jamais faire** : le vert vif #19D46A sur fond blanc (contraste de 1,97:1, trop faible). Utiliser le vert profond ou le noir.

## 3. Zone de protection
Gardez autour du logo un espace libre d'au moins **x = la moitié de la hauteur du M** de MEX, sur les quatre côtés. Cet espace s'adapte à la taille du logo.

## 4. Tailles minimales
| Version | Écran | Impression |
|---|---|---|
| Horizontale | 96 px de large | 25 mm de large |
| Empilée | 64 px de large | 18 mm de large |
| Symbole | 16 px (utiliser `mex-symbol-small-*` sous 32 px) | 6 mm |

## 5. Versions du symbole
- `mex-symbol-*` : dessin standard.
- `mex-symbol-small-*` : croix et boutons plus gros, pour 16 à 32 px.
- Les versions blanche, verte et `dark` ont des détails légèrement agrandis pour compenser l'effet de halo sur fond sombre.

## 6. Icônes web
Dossier `web/` : `favicon.ico`, `favicon.svg`, PNG 16/32/48, `apple-touch-icon.png`, `icon-192.png`, `icon-512.png`, `maskable-512.png`, `site.webmanifest` et `head-snippet.html` (à coller dans le `<head>`).

## 7. Typographie
Le wordmark MEX est dessiné en formes géométriques : aucune police n'est nécessaire et il ne faut pas le retaper. Pour le texte courant, une sans-serif neutre (Inter, par exemple) s'accorde bien.

## 8. À éviter
Ne pas étirer ou écraser · ne pas changer les couleurs hors palette · ne pas faire pivoter · pas d'ombre, de contour, de dégradé ou d'effet · ne pas réarranger ou redimensionner les parties d'un lockup · pas de fond chargé sans conteneur · ne pas recréer MEX avec une police.

## 9. Limites à connaître
- Aucune recherche d'antériorité n'a été faite : avant tout dépôt de marque, vérifiez dans les bases de marques et par recherche d'image inversée.
- Les fichiers PDF, AI et EPS ne sont pas fournis ; les SVG sont les fichiers maîtres.
