#!/usr/bin/env python3
"""Dérive du pack de logo (branding/svg) tout ce dont le logiciel a besoin.

    python3 branding/generer_icones.py

Les SVG de branding/svg sont les fichiers maîtres : ce programme ne redessine rien, il lit
leurs tracés et les convertit.

Sorties :
  branding/app/mex-icone-<taille>.png   icône de l'application, de 16 à 1024 px
  branding/mex.ico, branding/mex.icns   icônes Windows et macOS
  platform/windows/mex.ico              icône de l'exécutable Windows
  main/mex_logo.gen.h                   icône de la fenêtre, embarquée dans le moteur
  editor/mex_brand.gen.h                tracés du symbole et du mot MEX, dessinés par l'éditeur

Dépendances : Pillow, et rsvg-convert (paquet librsvg2-bin).
"""
import math
import os
import re
import subprocess

from PIL import Image

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.dirname(ICI)
SVG = os.path.join(ICI, 'svg')
APP = os.path.join(ICI, 'app')
TAILLES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
PAS_ARC = math.radians(3.0)  # un arc est remplacé par des segments de 3 degrés au plus


def lire(nom):
    with open(os.path.join(SVG, nom), encoding='utf-8') as f:
        return f.read()


def ecrire(chemin, contenu):
    if os.path.exists(chemin):
        with open(chemin, encoding='utf-8') as f:
            if f.read() == contenu:
                return
    with open(chemin, 'w', encoding='utf-8') as f:
        f.write(contenu)


# --- Lecture des tracés --------------------------------------------------------------------
def arc(x1, y1, rx, ry, rotation, grand, sens, x2, y2):
    """Points d'un arc SVG (sans le point de départ), d'après la spécification SVG, F.6.5."""
    if rx == 0 or ry == 0 or (x1 == x2 and y1 == y2):
        return [(x2, y2)]
    phi = math.radians(rotation)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    xp, yp = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    depassement = xp * xp / (rx * rx) + yp * yp / (ry * ry)
    if depassement > 1.0:
        rx, ry = rx * math.sqrt(depassement), ry * math.sqrt(depassement)
    num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp
    den = rx * rx * yp * yp + ry * ry * xp * xp
    k = math.sqrt(max(num / den, 0.0)) * (-1.0 if grand == sens else 1.0)
    cxp, cyp = k * rx * yp / ry, -k * ry * xp / rx
    cx, cy = cp * cxp - sp * cyp + (x1 + x2) / 2.0, sp * cxp + cp * cyp + (y1 + y2) / 2.0

    def angle(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = angle(1.0, 0.0, (xp - cxp) / rx, (yp - cyp) / ry)
    dt = angle((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry)
    if not sens and dt > 0:
        dt -= 2.0 * math.pi
    elif sens and dt < 0:
        dt += 2.0 * math.pi
    n = max(int(math.ceil(abs(dt) / PAS_ARC)), 1)
    points = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        points.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    points[-1] = (x2, y2)
    return points


def contours(d):
    """Découpe un attribut « d » (commandes absolues M, L, H, V, A, Z) en contours fermés."""
    jetons = re.findall(r'[MLHVAZ]|-?\d+(?:\.\d+)?', d)
    resultat, courant, x, y, i = [], [], 0.0, 0.0, 0
    commande = None
    nombres = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'A': 7, 'Z': 0}
    while i < len(jetons):
        if jetons[i] in nombres:
            commande = jetons[i]
            i += 1
            if commande == 'Z':
                if courant:
                    resultat.append(courant)
                courant = []
                continue
        elif commande is None or commande == 'Z':
            raise ValueError('tracé inattendu : ' + d[:40])
        a = [float(v) for v in jetons[i:i + nombres[commande]]]
        i += nombres[commande]
        if commande == 'M':
            if courant:
                resultat.append(courant)
            x, y = a
            courant = [(x, y)]
            commande = 'L'  # les couples suivants sont des segments
        elif commande == 'L':
            x, y = a
            courant.append((x, y))
        elif commande == 'H':
            x = a[0]
            courant.append((x, y))
        elif commande == 'V':
            y = a[0]
            courant.append((x, y))
        elif commande == 'A':
            points = arc(x, y, a[0], a[1], a[2], a[3] != 0, a[4] != 0, a[5], a[6])
            courant.extend(points)
            x, y = a[5], a[6]
    if courant:
        resultat.append(courant)
    # Un contour fermé ne répète pas son premier point.
    for c in resultat:
        if len(c) > 1 and abs(c[0][0] - c[-1][0]) < 1e-9 and abs(c[0][1] - c[-1][1]) < 1e-9:
            c.pop()
    return resultat


def trace(nom):
    """Tracé unique d'un fichier du pack : (contours, pair-impair, couleur)."""
    texte = lire(nom)
    chemins = re.findall(r'<path\b[^>]*>', texte)
    if len(chemins) != 1:
        raise ValueError('%s : un seul tracé attendu' % nom)
    d = re.search(r'\sd="([^"]+)"', chemins[0]).group(1)
    couleur = re.search(r'fill="(#[0-9A-Fa-f]{6})"', chemins[0]).group(1)
    return contours(d), 'fill-rule="evenodd"' in chemins[0], couleur


def bornes(liste):
    xs = [p[0] for c in liste for p in c]
    ys = [p[1] for c in liste for p in c]
    return min(xs), min(ys), max(xs), max(ys)


def flottant(valeur):
    """Écrit un nombre comme un littéral float du C++, au millième."""
    texte = ('%.3f' % valeur).rstrip('0').rstrip('.')
    if texte in ('-0', ''):
        texte = '0'
    return texte + ('f' if '.' in texte else '.0f')


# --- En-têtes C++ --------------------------------------------------------------------------
def tableau_octets(nom, donnees):
    lignes = []
    for i in range(0, len(donnees), 20):
        lignes.append('\t' + ', '.join('0x%02x' % b for b in donnees[i:i + 20]) + ',')
    return 'static const unsigned char %s[] = {\n%s\n};\nstatic const unsigned int %s_SIZE = %d;\n' % (nom, '\n'.join(lignes), nom, len(donnees))


def forme_cpp(nom, commentaire, fichier):
    liste, pair_impair, _ = trace(fichier)
    x0, y0, x1, y1 = bornes(liste)
    valeurs = ['%s, %s' % (flottant(p[0] - x0), flottant(p[1] - y0)) for c in liste for p in c]
    lignes = ['\t' + ', '.join(valeurs[i:i + 8]) + ',' for i in range(0, len(valeurs), 8)]
    return ('// %s (%s)\n'
            'static const float %s_POINTS[] = {\n%s\n};\n'
            'static const int %s_CONTOURS[] = { %s };\n'
            'static const MexBrandShape %s = { %s_POINTS, %s_CONTOURS, %d, %s, %s, %s };\n'
            % (commentaire, fichier, nom, '\n'.join(lignes), nom, ', '.join(str(len(c)) for c in liste),
               nom, nom, nom, len(liste), flottant(x1 - x0), flottant(y1 - y0), 'true' if pair_impair else 'false'))


def couleur_cpp(nom, hexa):
    return '#define %s 0x%s // %s\n' % (nom, hexa[1:].upper(), hexa.upper())


def logo_horizontal():
    """Place du symbole et du mot dans le logo horizontal, lue dans mex-horizontal-dark.svg."""
    texte = lire('mex-horizontal-dark.svg')
    largeur, hauteur = [float(v) for v in re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', texte).groups()]
    groupes = re.findall(r'<g transform="([^"]+)">', texte)
    if len(groupes) != 3:
        raise ValueError('mex-horizontal-dark.svg : structure inattendue')

    def lire_transformation(t):
        tx, ty = [float(v) for v in re.search(r'translate\(([-\d.]+)[ ,]+([-\d.]+)\)', t).groups()]
        e = re.search(r'scale\(([-\d.]+)\)', t)
        return tx, ty, float(e.group(1)) if e else 1.0

    # Symbole : deux groupes imbriqués ; mot : un groupe.
    ax, ay, _ = lire_transformation(groupes[0])
    bx, by, echelle_symbole = lire_transformation(groupes[1])
    mx, my, echelle_mot = lire_transformation(groupes[2])
    x0, y0, x1, y1 = bornes(trace('mex-symbol-dark.svg')[0])
    symbole = (ax + bx + echelle_symbole * x0, ay + by + echelle_symbole * y0, echelle_symbole * (x1 - x0), echelle_symbole * (y1 - y0))
    x0, y0, x1, y1 = bornes(trace('mex-wordmark-dark.svg')[0])
    mot = (mx + echelle_mot * x0, my + echelle_mot * y0, echelle_mot * (x1 - x0), echelle_mot * (y1 - y0))
    return largeur, hauteur, symbole, mot


def main():
    os.makedirs(APP, exist_ok=True)
    # Icône de l'application : le fichier mex-app-icon.svg du pack, rendu à chaque taille.
    for t in TAILLES:
        subprocess.check_call(['rsvg-convert', os.path.join(SVG, 'mex-app-icon.svg'), '-w', str(t), '-h', str(t),
                               '-o', os.path.join(APP, 'mex-icone-%d.png' % t)])
    images = {t: Image.open(os.path.join(APP, 'mex-icone-%d.png' % t)).convert('RGBA') for t in TAILLES}
    images[256].save(os.path.join(ICI, 'mex.ico'), format='ICO', sizes=[(t, t) for t in (16, 24, 32, 48, 64, 128, 256)],
                     append_images=[images[t] for t in (16, 24, 32, 48, 64, 128)])
    images[1024].save(os.path.join(ICI, 'mex.icns'), format='ICNS', append_images=[images[t] for t in (16, 32, 64, 128, 256, 512)])
    os.makedirs(os.path.join(RACINE, 'platform', 'windows'), exist_ok=True)
    with open(os.path.join(ICI, 'mex.ico'), 'rb') as src, open(os.path.join(RACINE, 'platform', 'windows', 'mex.ico'), 'wb') as dst:
        dst.write(src.read())

    avis = '// Fichier généré par branding/generer_icones.py : ne pas modifier à la main.\n'
    with open(os.path.join(APP, 'mex-icone-128.png'), 'rb') as f:
        ecrire(os.path.join(RACINE, 'main', 'mex_logo.gen.h'),
               avis + '// Icône de MEX, PNG 128 x 128 (icône de fenêtre, projets sans icône).\n#pragma once\n\n'
               + tableau_octets('MEX_ICON_PNG', f.read()))

    largeur, hauteur, symbole, mot = logo_horizontal()
    couleurs = {
        'MEX_BRAND_GREEN': trace('mex-symbol-dark.svg')[2],
        'MEX_BRAND_DEEP_GREEN': trace('mex-symbol-light.svg')[2],
        'MEX_BRAND_BLACK': trace('mex-wordmark-light.svg')[2],
        'MEX_BRAND_OFF_WHITE': trace('mex-wordmark-dark.svg')[2],
    }
    corps = avis + ('// Tracés du logo de MEX, lus dans branding/svg. Coordonnées dans le repère des SVG, ramenées\n'
                    '// au coin supérieur gauche de chaque forme ; les arcs sont remplacés par des segments.\n'
                    '#pragma once\n\n'
                    'struct MexBrandShape {\n'
                    '\tconst float *points; // x, y, x, y...\n'
                    '\tconst int *contours; // nombre de points de chaque contour fermé\n'
                    '\tint contour_count;\n'
                    '\tfloat width;\n'
                    '\tfloat height;\n'
                    '\tbool even_odd; // règle de remplissage : pair-impair, sinon non nul\n'
                    '};\n\n')
    corps += '// Couleurs de la marque (0xRRVVBB).\n'
    for nom, hexa in couleurs.items():
        corps += couleur_cpp(nom, hexa)
    corps += '\n'
    corps += forme_cpp('MEX_BRAND_SYMBOL', 'Symbole, dessin standard : fonds clairs', 'mex-symbol-light.svg') + '\n'
    corps += forme_cpp('MEX_BRAND_SYMBOL_DARK', 'Symbole, détails agrandis : fonds sombres', 'mex-symbol-dark.svg') + '\n'
    corps += forme_cpp('MEX_BRAND_SYMBOL_SMALL', 'Symbole pour les petites tailles, sous 32 px', 'mex-symbol-small-dark.svg') + '\n'
    corps += forme_cpp('MEX_BRAND_WORDMARK', 'Mot MEX', 'mex-wordmark-dark.svg') + '\n'
    corps += ('// Logo horizontal (mex-horizontal-*.svg) : taille de l\'ensemble, puis place du symbole et du mot.\n'
              'static const float MEX_BRAND_LOCKUP_WIDTH = %s;\n'
              'static const float MEX_BRAND_LOCKUP_HEIGHT = %s;\n'
              'static const float MEX_BRAND_LOCKUP_SYMBOL[4] = { %s, %s, %s, %s }; // x, y, largeur, hauteur\n'
              'static const float MEX_BRAND_LOCKUP_WORDMARK[4] = { %s, %s, %s, %s };\n'
              % tuple(flottant(v) for v in (largeur, hauteur) + symbole + mot))
    ecrire(os.path.join(RACINE, 'editor', 'mex_brand.gen.h'), corps)
    print('Icônes et tracés générés depuis', SVG)


if __name__ == '__main__':
    main()
