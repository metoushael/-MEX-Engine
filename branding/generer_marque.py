#!/usr/bin/env python3
"""Génère l'identité visuelle de MEX (Metoushela Engine X).

Tout part de la géométrie décrite ci-dessous : le symbole est construit par le calcul, les
textes sont convertis en tracés (aucune police n'est nécessaire pour afficher les SVG).

    python3 branding/generer_marque.py

Dépendances : fonttools, Pillow, et rsvg-convert (paquet librsvg2-bin) pour les PNG.
Sorties : branding/svg, branding/png, branding/mex.ico, branding/mex.icns, branding/planche.png,
ainsi que les fichiers embarqués dans le moteur (main/mex_logo.gen.h, platform/windows/mex.ico).
"""
import os
import subprocess

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from PIL import Image

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.dirname(ICI)

# --- Couleurs de la marque ---------------------------------------------------------------
INDIGO = '#11132C'       # Indigo nuit : fond principal
INDIGO_HAUT = '#1B1E45'  # haut du dégradé de l'icône
INDIGO_BAS = '#0C0E22'   # bas du dégradé de l'icône
ARDOISE = '#2A2E5A'      # filets, panneaux
LAVANDE = '#A0A6CD'      # texte secondaire sur fond sombre
BLANC = '#FFFFFF'
ORANGE = '#FF7A00'       # Orange MEX : couleur d'accent
AMBRE = '#FFA31A'        # haut du dégradé orange
BRAISE = '#E85400'       # bas du dégradé orange
BRUME = '#F4F5FA'        # fond clair

# --- Géométrie du symbole ----------------------------------------------------------------
# Repère : x = 0 au centre, y = 0 en haut du M, y = BASE à la ligne de base. Toutes les
# obliques ont la même pente ; u = x - y/PENTE et v = x + y/PENTE repèrent les deux familles.
PENTE = 7.0 / 6.0
DEMI = 173.0     # demi-largeur du M
FUT = 63.0       # largeur des jambages verticaux
OBLIQUE = 80.0   # largeur horizontale des obliques
U_EXT = -101.0   # bord extérieur de l'oblique gauche
U_INT = U_EXT - OBLIQUE
BASE = 312.0
POINTE = 36.0    # dépassement de l'orange au-dessus du croisement
ECART = 32.0     # espace entre le pied des jambages et le X
LARGEUR = 2 * DEMI


def _uv(u, v):
    return ((u + v) / 2.0, (v - u) / 2.0 * PENTE)


def _a_y(u, y):
    return (u + y / PENTE, y)


_V_POINTE = -U_EXT - POINTE
_V_COUPE = -U_EXT - ECART
_X_FUT = -DEMI + FUT

# Moitié gauche du M (jambage + oblique jusqu'au croisement) et jambe du X qui la prolonge.
M_GAUCHE = [
    (-DEMI, 0), (U_EXT, 0), _uv(U_EXT, _V_POINTE), _uv(U_INT, _V_POINTE),
    (_X_FUT, (_X_FUT - U_INT) * PENTE), (_X_FUT, (_V_COUPE - _X_FUT) * PENTE), (-DEMI, (_V_COUPE + DEMI) * PENTE),
]
JAMBE_A = [_uv(U_EXT, _V_POINTE), _a_y(U_EXT, BASE), _a_y(U_INT, BASE), _uv(U_INT, _V_POINTE)]


def _miroir(points):
    return [(-x, y) for x, y in reversed(points)]


def _trace(points, dx, dy, k):
    return 'M' + ' L'.join('%.2f %.2f' % (dx + x * k, dy + y * k) for x, y in points) + ' Z'


_compteur = [0]


def symbole(cx, haut, hauteur, m=BLANC, x='degrade'):
    """Symbole centré en cx, dont le haut est à `haut`. Renvoie (defs, tracés).
    x : 'degrade' (deux tons), 'aplat' (Orange MEX) ou une couleur."""
    k = hauteur / BASE
    defs = ''
    corps = '<path d="%s %s" fill="%s"/>\n' % (_trace(M_GAUCHE, cx, haut, k), _trace(_miroir(M_GAUCHE), cx, haut, k), m)
    if x == 'degrade':
        _compteur[0] += 1
        n = _compteur[0]
        for nom, c0, c1 in (('ambre', AMBRE, '#FF6F00'), ('braise', '#FF8A00', BRAISE)):
            defs += ('<linearGradient id="%s%d" gradientUnits="userSpaceOnUse" x1="0" y1="%.1f" x2="0" y2="%.1f">'
                     '<stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>\n') % (nom, n, haut, haut + hauteur, c0, c1)
        corps += '<path d="%s" fill="url(#braise%d)"/>\n' % (_trace(_miroir(JAMBE_A), cx, haut, k), n)
        corps += '<path d="%s" fill="url(#ambre%d)"/>\n' % (_trace(JAMBE_A, cx, haut, k), n)
    else:
        couleur = ORANGE if x == 'aplat' else x
        corps += '<path d="%s %s" fill="%s"/>\n' % (_trace(JAMBE_A, cx, haut, k), _trace(_miroir(JAMBE_A), cx, haut, k), couleur)
    return defs, corps


# --- Textes convertis en tracés ----------------------------------------------------------
class Police:
    def __init__(self, fichier):
        self.font = TTFont(os.path.join(ICI, 'polices', fichier))
        self.glyphes = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.upm = self.font['head'].unitsPerEm
        self.capitale = self.font['OS/2'].sCapHeight

    def corps_pour_capitale(self, hauteur):
        return hauteur * self.upm / float(self.capitale)

    def _bornes(self, nom):
        pen = BoundsPen(self.glyphes)
        self.glyphes[nom].draw(pen)
        return pen.bounds or (0, 0, 0, 0)

    def mesure(self, texte, corps, approche=0.0):
        """Largeur visible (du bord gauche du premier glyphe au bord droit du dernier)."""
        k = corps / self.upm
        noms = [self.cmap[ord(c)] for c in texte]
        avance = sum(self.glyphes[n].width for n in noms) * k + approche * (len(noms) - 1)
        gauche = self._bornes(noms[0])[0] * k
        droite = (self.glyphes[noms[-1]].width - self._bornes(noms[-1])[2]) * k
        return avance - gauche - droite

    def tracer(self, texte, corps, x, base, couleur, approche=0.0, couleurs=None):
        """Texte dont le bord gauche visible est en x. `couleurs` : couleur par caractère."""
        k = corps / self.upm
        noms = [self.cmap[ord(c)] for c in texte]
        cx = x - self._bornes(noms[0])[0] * k
        sortie = ''
        for i, nom in enumerate(noms):
            pen = SVGPathPen(self.glyphes)
            self.glyphes[nom].draw(TransformPen(pen, (k, 0, 0, -k, cx, base)))
            d = pen.getCommands()
            if d:
                sortie += '<path d="%s" fill="%s"/>\n' % (d, couleurs[i] if couleurs else couleur)
            cx += self.glyphes[nom].width * k + approche
        return sortie


GRAS = Police('Poppins-Bold.ttf')
MOYEN = Police('Poppins-Medium.ttf')
SIGNATURE = 'METOUSHELA ENGINE X'


def svg(largeur, hauteur, defs, corps):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" width="%g" height="%g">\n'
            '<title>MEX — Metoushela Engine X</title>\n<defs>\n%s</defs>\n%s</svg>\n') % (largeur, hauteur, largeur, hauteur, defs, corps)


def svg_symbole(m, x, marge=FUT):
    h = BASE
    defs, corps = symbole(marge + DEMI, marge, h, m, x)
    return svg(LARGEUR + 2 * marge, h + 2 * marge, defs, corps)


def svg_icone(petit=False):
    """Icône d'application 1024 x 1024. `petit` : variante pour 16 à 32 px (symbole plus
    grand, orange en aplat) qui reste lisible une fois réduite."""
    taille = 1024.0
    part = 0.76 if petit else 0.60
    h = taille * part * BASE / LARGEUR
    defs, corps = symbole(taille / 2, (taille - h) / 2, h, BLANC, 'aplat' if petit else 'degrade')
    defs += ('<linearGradient id="fond" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/>'
             '<stop offset="1" stop-color="%s"/></linearGradient>\n') % (INDIGO_HAUT, INDIGO_BAS)
    fond = '<rect width="1024" height="1024" rx="%d" fill="url(#fond)"/>\n' % (250 if petit else 230)
    return svg(taille, taille, defs, fond + corps)


def bloc_logo(x, y, hauteur, clair=False):
    """Logo horizontal : symbole, mot « MEX » et signature. Renvoie (defs, tracés, largeur)."""
    m = INDIGO if clair else BLANC
    k = hauteur / BASE
    largeur_symbole = LARGEUR * k
    defs, corps = symbole(x + largeur_symbole / 2, y, hauteur, m, 'degrade')
    # Le mot occupe les 62 % supérieurs de la hauteur du symbole, la signature s'aligne sur
    # la ligne de base et prend exactement la largeur du mot.
    cap_mot = hauteur * 0.62
    corps_mot = GRAS.corps_pour_capitale(cap_mot)
    approche_mot = corps_mot * 0.035
    x_texte = x + largeur_symbole + hauteur * 0.30
    corps += GRAS.tracer('MEX', corps_mot, x_texte, y + cap_mot, m, approche_mot, [m, m, ORANGE])
    largeur_mot = GRAS.mesure('MEX', corps_mot, approche_mot)
    cap_sig = hauteur * 0.105
    corps_sig = MOYEN.corps_pour_capitale(cap_sig)
    sans_approche = MOYEN.mesure(SIGNATURE, corps_sig, 0)
    approche_sig = (largeur_mot - sans_approche) / (len(SIGNATURE) - 1)
    corps += MOYEN.tracer(SIGNATURE, corps_sig, x_texte, y + hauteur, '#5A5F8C' if clair else LAVANDE, approche_sig)
    return defs, corps, largeur_symbole + hauteur * 0.30 + largeur_mot


def svg_logo_horizontal(clair=False, fond=None):
    h, marge = BASE, FUT  # la marge du fichier est la zone de protection
    defs, corps, largeur = bloc_logo(marge, marge, h, clair)
    total_l, total_h = largeur + 2 * marge, h + 2 * marge
    if fond:
        corps = '<rect width="%g" height="%g" fill="%s"/>\n' % (total_l, total_h, fond) + corps
    return svg(total_l, total_h, defs, corps)


def svg_logo_vertical(clair=False):
    m = INDIGO if clair else BLANC
    h, marge = BASE, FUT
    cap_mot = 150.0
    corps_mot = GRAS.corps_pour_capitale(cap_mot)
    approche_mot = corps_mot * 0.05
    largeur_mot = GRAS.mesure('MEX', corps_mot, approche_mot)
    cap_sig = 26.0
    corps_sig = MOYEN.corps_pour_capitale(cap_sig)
    largeur_bloc = max(LARGEUR, largeur_mot)
    approche_sig = (largeur_bloc - MOYEN.mesure(SIGNATURE, corps_sig, 0)) / (len(SIGNATURE) - 1)
    total_l = largeur_bloc + 2 * marge
    cx = total_l / 2
    defs, corps = symbole(cx, marge, h, m, 'degrade')
    y = marge + h + 70 + cap_mot
    corps += GRAS.tracer('MEX', corps_mot, cx - largeur_mot / 2, y, m, approche_mot, [m, m, ORANGE])
    y += 44 + cap_sig
    corps += MOYEN.tracer(SIGNATURE, corps_sig, cx - largeur_bloc / 2, y, '#5A5F8C' if clair else LAVANDE, approche_sig)
    return svg(total_l, y + marge, defs, corps)


def motif_obliques(largeur, hauteur, pas=64.0, couleur=ARDOISE, opacite=0.35):
    """Filets parallèles aux obliques du symbole : le fond des bannières."""
    lignes = ''
    x = -hauteur / PENTE
    while x < largeur:
        lignes += '<line x1="%.1f" y1="0" x2="%.1f" y2="%.1f"/>\n' % (x, x + hauteur / PENTE, hauteur)
        x += pas
    return '<g stroke="%s" stroke-width="1.5" opacity="%.2f">\n%s</g>\n' % (couleur, opacite, lignes)


def svg_banniere():
    l, h = 1280.0, 420.0
    defs = ('<linearGradient id="fondb" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="%s"/>'
            '<stop offset="1" stop-color="%s"/></linearGradient>\n'
            '<radialGradient id="lueur" cx="0.5" cy="0.45" r="0.6"><stop offset="0" stop-color="#2A2F6E" stop-opacity="0.55"/>'
            '<stop offset="1" stop-color="#2A2F6E" stop-opacity="0"/></radialGradient>\n') % (INDIGO_HAUT, INDIGO_BAS)
    corps = '<rect width="%g" height="%g" fill="url(#fondb)"/>\n' % (l, h)
    corps += motif_obliques(l, h)
    corps += '<rect width="%g" height="%g" fill="url(#lueur)"/>\n' % (l, h)
    haut_logo = 170.0
    d, c, largeur = bloc_logo(0, 0, haut_logo)
    x0 = (l - largeur) / 2
    y0 = 92.0
    defs += d
    corps += '<g transform="translate(%.1f %.1f)">\n%s</g>\n' % (x0, y0, c)
    accroche = 'Moteur de jeu 2D et 3D  ·  Éditeur intégré  ·  MexScript'
    corps_acc = 21.0
    largeur_acc = MOYEN.mesure(accroche, corps_acc, 0.6)
    corps += MOYEN.tracer(accroche, corps_acc, (l - largeur_acc) / 2, y0 + haut_logo + 78, LAVANDE, 0.6)
    corps += '<rect x="%.1f" y="%.1f" width="72" height="4" rx="2" fill="%s"/>\n' % (l / 2 - 36, y0 + haut_logo + 34, ORANGE)
    return svg(l, h, defs, corps)


def svg_planche():
    """Planche de marque : logos, icône, couleurs et typographie sur une seule page."""
    l, h = 1600.0, 1180.0
    defs = ''
    corps = '<rect width="%g" height="%g" fill="#090A18"/>\n' % (l, h)

    def titre(texte, x, y, couleur=LAVANDE):
        return MOYEN.tracer(texte.upper(), 15, x, y, couleur, 2.2)

    def panneau(x, y, w, hh, couleur, rayon=18):
        return '<rect x="%g" y="%g" width="%g" height="%g" rx="%d" fill="%s"/>\n' % (x, y, w, hh, rayon, couleur)

    # En-tête
    corps += GRAS.tracer('MEX', 30, 48, 70, BLANC, 1.5, [BLANC, BLANC, ORANGE])
    corps += MOYEN.tracer('Identité visuelle  ·  Metoushela Engine X', 17, 138, 68, LAVANDE, 0.4)
    corps += '<rect x="48" y="92" width="1504" height="1.5" fill="%s"/>\n' % ARDOISE

    # 1. Logo principal
    corps += titre('Logo principal', 48, 132)
    corps += panneau(48, 150, 900, 330, INDIGO)
    d, c, largeur = bloc_logo(0, 0, 170)
    defs += d
    corps += '<g transform="translate(%.1f %.1f)">\n%s</g>\n' % (48 + (900 - largeur) / 2, 150 + 80, c)

    # 2. Icône d'application
    corps += titre("Icône d'application", 990, 132)
    corps += panneau(990, 150, 562, 330, '#14162F')
    corps += '<image href="png/mex-icone-256.png" x="1020" y="192" width="246" height="246"/>\n'
    x = 1300
    for taille in (96, 64, 48):
        corps += '<image href="png/mex-icone-%d.png" x="%d" y="%d" width="%d" height="%d"/>\n' % (128 if taille == 96 else taille, x, 300 - taille, taille, taille)
        x += taille + 18
    x = 1300
    for taille in (32, 24, 16):
        corps += '<image href="png/mex-icone-%d.png" x="%d" y="%d" width="%d" height="%d"/>\n' % (taille, x, 372 - taille, taille, taille)
        corps += MOYEN.tracer(str(taille), 11, x, 392, LAVANDE, 0.3)
        x += taille + 26
    corps += MOYEN.tracer('De 16 à 1024 px', 13, 1300, 430, LAVANDE, 0.3)

    # 3. Déclinaisons
    corps += titre('Déclinaisons', 48, 528)
    largeur_case = 286.0
    cases = (
        (BRUME, INDIGO, 'degrade', 'Fond clair'),
        (INDIGO, BLANC, 'degrade', 'Fond sombre'),
        (ORANGE, BLANC, BLANC, 'Monochrome blanc'),
        (BLANC, '#0B0C14', '#0B0C14', 'Monochrome noir'),
        ('#1C1F26', BLANC, 'aplat', 'Aplat (petites tailles)'),
    )
    for i, (fond, m, x_mode, nom) in enumerate(cases):
        px = 48 + i * (largeur_case + 18.5)
        corps += panneau(px, 546, largeur_case, 210, fond)
        d, c = symbole(px + largeur_case / 2, 546 + 55, 100, m, x_mode)
        defs += d
        corps += c
        corps += MOYEN.tracer(nom, 13, px, 784, LAVANDE, 0.3)

    # 4. Couleurs
    corps += titre('Couleurs', 48, 846)
    couleurs = (
        (INDIGO, 'Indigo nuit', '17 · 19 · 44', BLANC),
        (ORANGE, 'Orange MEX', '255 · 122 · 0', INDIGO),
        (AMBRE, 'Ambre', '255 · 163 · 26', INDIGO),
        (BRAISE, 'Braise', '232 · 84 · 0', BLANC),
        (BLANC, 'Blanc', '255 · 255 · 255', INDIGO),
        (LAVANDE, 'Lavande', '160 · 166 · 205', INDIGO),
    )
    largeur_c = 138.0
    for i, (couleur, nom, rvb, texte) in enumerate(couleurs):
        px = 48 + i * (largeur_c + 14)
        corps += panneau(px, 864, largeur_c, 150, couleur, 14)
        if couleur == INDIGO:
            corps += '<rect x="%g" y="864" width="%g" height="150" rx="14" fill="none" stroke="%s" stroke-width="1.5"/>\n' % (px, largeur_c, ARDOISE)
        corps += GRAS.tracer(couleur, 15, px + 14, 990, texte, 0.4)
        corps += MOYEN.tracer(nom, 14, px, 1042, BLANC, 0.3)
        corps += MOYEN.tracer('RVB ' + rvb, 11.5, px, 1062, LAVANDE, 0.2)
    # Dégradé de la marque
    defs += ('<linearGradient id="bande" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="%s"/>'
             '<stop offset="0.5" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>\n') % (AMBRE, ORANGE, BRAISE)
    corps += '<rect x="48" y="1096" width="898" height="10" rx="5" fill="url(#bande)"/>\n'
    corps += MOYEN.tracer('Dégradé de marque : Ambre, Orange MEX, Braise', 12, 48, 1134, LAVANDE, 0.3)

    # 5. Typographie
    corps += titre('Typographie', 990, 846)
    corps += panneau(990, 864, 562, 270, '#14162F')
    corps += GRAS.tracer('Poppins', 54, 1020, 940, BLANC, 0.5)
    corps += MOYEN.tracer('Bold pour le mot « MEX » et les titres', 14, 1020, 972, LAVANDE, 0.3)
    corps += GRAS.tracer('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 19, 1020, 1016, BLANC, 1.2)
    corps += MOYEN.tracer('abcdefghijklmnopqrstuvwxyz 0123456789', 17, 1020, 1048, LAVANDE, 0.6)
    corps += MOYEN.tracer(SIGNATURE, 15, 1020, 1092, ORANGE, 5.0)
    corps += MOYEN.tracer('Medium, capitales très espacées, pour la signature', 12, 1020, 1114, LAVANDE, 0.3)
    return svg(l, h, defs, corps)


def svg_construction():
    """Schéma de construction : pente unique, module x (largeur d'un jambage) et zone de protection."""
    l, h = 1400.0, 600.0
    defs = ''
    corps = '<rect width="%g" height="%g" fill="%s"/>\n' % (l, h, INDIGO)
    guide = '#5A62A8'

    # Symbole seul, avec ses lignes de construction.
    k = 1.0
    cx, haut = 300.0, 150.0
    x0, x1 = cx - DEMI, cx + DEMI
    y1 = haut + BASE
    corps += '<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" stroke="%s" stroke-width="1.5" stroke-dasharray="7 6"/>\n' % (
        x0 - FUT, haut - FUT, LARGEUR + 2 * FUT, BASE + 2 * FUT, ORANGE)
    lignes = ''
    for y in (haut, y1):
        lignes += '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"/>\n' % (x0 - 90, y, x1 + 90, y)
    for x in (x0, x0 + FUT, x1 - FUT, x1, cx):
        lignes += '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"/>\n' % (x, haut - 90, x, y1 + 90)
    # Prolongement des obliques : une seule pente pour tout le dessin.
    for u in (U_EXT, U_INT):
        for signe in (1, -1):
            ya, yb = -70.0, BASE + 70.0
            lignes += '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"/>\n' % (
                cx + signe * (u + ya / PENTE), haut + ya, cx + signe * (u + yb / PENTE), haut + yb)
    corps += '<g stroke="%s" stroke-width="1" opacity="0.75">\n%s</g>\n' % (guide, lignes)
    d, c = symbole(cx, haut, BASE * k, BLANC, 'degrade')
    defs += d
    corps += c

    def cote(xa, ya, xb, yb, texte, dx=0.0, dy=0.0):
        s = '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="2"/>\n' % (xa, ya, xb, yb, ORANGE)
        for (px, py) in ((xa, ya), (xb, yb)):
            if ya == yb:
                s += '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="2"/>\n' % (px, py - 6, px, py + 6, ORANGE)
            else:
                s += '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="2"/>\n' % (px - 6, py, px + 6, py, ORANGE)
        s += MOYEN.tracer(texte, 15, (xa + xb) / 2 + dx, (ya + yb) / 2 + dy, ORANGE, 0.3)
        return s

    corps += cote(x0, haut - 28, x0 + FUT, haut - 28, 'x', -5, -12)
    corps += cote(x0 - FUT, y1 + 28, x0, y1 + 28, 'x', -5, 26)
    corps += cote(x1 + 28, y1, x1 + 28, y1 + FUT, 'x', 12, 5)
    corps += MOYEN.tracer('SYMBOLE', 13, 60, 40, LAVANDE, 2.0)
    corps += MOYEN.tracer('Une seule pente (7 pour 6, environ 49°), jambages de largeur x,', 13, 60, 556, LAVANDE, 0.2)
    corps += MOYEN.tracer("zone de protection x tout autour (pointillés).", 13, 60, 576, LAVANDE, 0.2)

    # Logo horizontal et sa zone de protection.
    hl = 170.0
    kx = FUT * hl / BASE
    d, c, largeur = bloc_logo(0, 0, hl)
    defs += d
    lx, ly = 740.0, 216.0
    corps += '<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" stroke="%s" stroke-width="1.5" stroke-dasharray="7 6"/>\n' % (
        lx - kx, ly - kx, largeur + 2 * kx, hl + 2 * kx, ORANGE)
    corps += '<g stroke="%s" stroke-width="1" opacity="0.75">\n' % guide
    for y in (ly, ly + hl * 0.62, ly + hl * (1 - 0.105), ly + hl):
        corps += '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"/>\n' % (lx - 60, y, lx + largeur + 60, y)
    corps += '</g>\n'
    corps += '<g transform="translate(%.1f %.1f)">\n%s</g>\n' % (lx, ly, c)
    corps += cote(lx - kx, ly + hl + kx + 24, lx, ly + hl + kx + 24, 'x', -5, 26)
    corps += cote(lx + largeur + kx + 24, ly - kx, lx + largeur + kx + 24, ly, 'x', 12, 5)
    corps += MOYEN.tracer('LOGO HORIZONTAL', 13, 700, 40, LAVANDE, 2.0)
    corps += MOYEN.tracer('Le mot « MEX » occupe 62 % de la hauteur du symbole ; la signature repose', 13, 700, 556, LAVANDE, 0.2)
    corps += MOYEN.tracer('sur la même ligne de base et prend exactement la largeur du mot.', 13, 700, 576, LAVANDE, 0.2)
    return svg(l, h, defs, corps)


def svg_interdits():
    """Les usages à éviter, dessinés."""
    l, h = 1280.0, 330.0
    defs = ''
    corps = '<rect width="%g" height="%g" fill="#090A18"/>\n' % (l, h)
    cas = (
        ('Ne pas déformer', INDIGO, 'scale(1.5 0.72)', BLANC, 'degrade', None),
        ('Ne pas incliner', INDIGO, 'rotate(-16)', BLANC, 'degrade', None),
        ('Ne pas changer les couleurs', INDIGO, '', '#6FE3A1', '#D94FD0', None),
        ('Pas de fond qui nuit au contraste', '#F29A2E', '', BLANC, 'degrade', None),
        ('Pas de contour ni d\'ombre', INDIGO, '', BLANC, 'degrade', 'contour'),
    )
    largeur_case = 228.0
    for i, (nom, fond, transfo, m, x_mode, effet) in enumerate(cas):
        px = 40 + i * (largeur_case + 15)
        corps += '<rect x="%.1f" y="36" width="%.1f" height="200" rx="16" fill="%s"/>\n' % (px, largeur_case, fond)
        d, c = symbole(0, -50, 100, m, x_mode)
        defs += d
        if effet == 'contour':
            c = '<g stroke="#FFE14A" stroke-width="7" stroke-linejoin="round">%s</g>' % c
        corps += '<g transform="translate(%.1f 136) %s">%s</g>\n' % (px + largeur_case / 2, transfo, c)
        # Croix rouge
        corps += ('<circle cx="%.1f" cy="62" r="13" fill="#E5484D"/>'
                  '<path d="M%.1f 56 l12 12 M%.1f 56 l-12 12" stroke="#fff" stroke-width="2.6" stroke-linecap="round"/>\n') % (
                      px + largeur_case - 26, px + largeur_case - 32, px + largeur_case - 20)
        corps += MOYEN.tracer(nom, 13, px, 266, LAVANDE, 0.2)
    return svg(l, h, defs, corps)


# --- Écriture des fichiers ---------------------------------------------------------------
def ecrire(chemin, contenu):
    os.makedirs(os.path.dirname(chemin), exist_ok=True)
    if os.path.exists(chemin):
        with open(chemin, encoding='utf-8') as f:
            if f.read() == contenu:
                return  # inchangé : on ne touche pas au fichier (pas de recompilation inutile)
    with open(chemin, 'w', encoding='utf-8') as f:
        f.write(contenu)


def rendre(source, sortie, largeur=None, hauteur=None):
    os.makedirs(os.path.dirname(sortie), exist_ok=True)
    commande = ['rsvg-convert', source, '-o', sortie]
    if largeur:
        commande += ['-w', str(int(largeur))]
    if hauteur:
        commande += ['-h', str(int(hauteur))]
    subprocess.check_call(commande, cwd=ICI)


def en_tete_c(chemin, nom, donnees, commentaire):
    lignes = []
    for i in range(0, len(donnees), 20):
        lignes.append('\t' + ', '.join('0x%02x' % b for b in donnees[i:i + 20]) + ',')
    ecrire(chemin, '// Fichier généré par branding/generer_marque.py : ne pas modifier à la main.\n'
           '// %s\n#pragma once\n\nstatic const unsigned char %s[] = {\n%s\n};\nstatic const unsigned int %s_SIZE = %d;\n'
           % (commentaire, nom, '\n'.join(lignes), nom, len(donnees)))


def main():
    S = os.path.join(ICI, 'svg')
    P = os.path.join(ICI, 'png')
    fichiers = {
        'mex-symbole.svg': svg_symbole(BLANC, 'degrade'),
        'mex-symbole-clair.svg': svg_symbole(INDIGO, 'degrade'),
        'mex-symbole-blanc.svg': svg_symbole(BLANC, BLANC),
        'mex-symbole-noir.svg': svg_symbole('#0B0C14', '#0B0C14'),
        'mex-icone.svg': svg_icone(False),
        'mex-icone-petit.svg': svg_icone(True),
        'mex-logo-horizontal.svg': svg_logo_horizontal(False),
        'mex-logo-horizontal-clair.svg': svg_logo_horizontal(True),
        'mex-logo-vertical.svg': svg_logo_vertical(False),
        'mex-logo-vertical-clair.svg': svg_logo_vertical(True),
        'mex-banniere.svg': svg_banniere(),
    }
    for nom, contenu in fichiers.items():
        ecrire(os.path.join(S, nom), contenu)

    # Icônes : la variante « petit » en dessous de 48 px.
    tailles = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
    for t in tailles:
        source = 'mex-icone-petit.svg' if t < 48 else 'mex-icone.svg'
        rendre(os.path.join(S, source), os.path.join(P, 'mex-icone-%d.png' % t), t, t)
    for nom, largeur in (('mex-symbole', 512), ('mex-symbole-clair', 512), ('mex-symbole-blanc', 512), ('mex-symbole-noir', 512),
                         ('mex-logo-horizontal', 1200), ('mex-logo-horizontal-clair', 1200),
                         ('mex-logo-vertical', 800), ('mex-logo-vertical-clair', 800), ('mex-banniere', 1280)):
        rendre(os.path.join(S, nom + '.svg'), os.path.join(P, nom + '.png'), largeur)

    # Formats d'icône des systèmes.
    images = {t: Image.open(os.path.join(P, 'mex-icone-%d.png' % t)).convert('RGBA') for t in tailles}
    images[256].save(os.path.join(ICI, 'mex.ico'), format='ICO', sizes=[(t, t) for t in (16, 24, 32, 48, 64, 128, 256)],
                     append_images=[images[t] for t in (16, 24, 32, 48, 64, 128)])
    images[1024].save(os.path.join(ICI, 'mex.icns'), format='ICNS', append_images=[images[t] for t in (16, 32, 64, 128, 256, 512)])

    # Planche de marque (elle inclut les PNG de l'icône : rendue depuis le dossier branding).
    ecrire(os.path.join(ICI, 'planche.svg'), svg_planche())
    rendre(os.path.join(ICI, 'planche.svg'), os.path.join(ICI, 'planche.png'), 1600)
    os.remove(os.path.join(ICI, 'planche.svg'))
    for nom, contenu in (('construction', svg_construction()), ('interdits', svg_interdits())):
        ecrire(os.path.join(ICI, nom + '.svg'), contenu)
        rendre(os.path.join(ICI, nom + '.svg'), os.path.join(ICI, nom + '.png'), 1400 if nom == 'construction' else 1280)
        os.remove(os.path.join(ICI, nom + '.svg'))

    # Fichiers embarqués dans le moteur.
    with open(os.path.join(P, 'mex-icone-128.png'), 'rb') as f:
        en_tete_c(os.path.join(RACINE, 'main', 'mex_logo.gen.h'), 'MEX_ICON_PNG', f.read(), "Icône de MEX, PNG 128 x 128 (icône de fenêtre, éditeur).")
    os.makedirs(os.path.join(RACINE, 'platform', 'windows'), exist_ok=True)
    with open(os.path.join(ICI, 'mex.ico'), 'rb') as src, open(os.path.join(RACINE, 'platform', 'windows', 'mex.ico'), 'wb') as dst:
        dst.write(src.read())
    print('Identité visuelle générée dans', ICI)


if __name__ == '__main__':
    main()
