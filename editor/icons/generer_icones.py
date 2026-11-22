#!/usr/bin/env python3
"""Génère editor/editor_icons.gen.h : les icônes de l'éditeur.

Les icônes viennent de la police Lucide (thirdparty/lucide, licence ISC). Seuls les glyphes
listés ci-dessous sont conservés : la police embarquée ne pèse que quelques kilo-octets.

Pour ajouter une icône : choisissez-la sur https://lucide.dev/icons, ajoutez une ligne
« NOM = nom-lucide » à ICONES, puis relancez :

    pip install fonttools
    python3 editor/icons/generer_icones.py
"""
import io
import json
import os

from fontTools import subset
from fontTools.ttLib import TTFont

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.dirname(os.path.dirname(ICI))
LUCIDE = os.path.join(RACINE, 'thirdparty', 'lucide')

ICONES = """
# --- Actions générales
PLAY = play
PLAY_SCENE = circle-play
PAUSE = pause
STOP = square
PLUS = plus
MINUS = minus
CLOSE = x
CHECK = check
SEARCH = search
SETTINGS = settings
REFRESH = refresh-cw
SAVE = save
TRASH = trash-2
COPY = copy
EDIT = pencil
RENAME = text-cursor-input
UNDO = undo-2
REDO = redo-2
STAR = star
INFO = info
WARNING = triangle-alert
ERROR = circle-x
HELP = circle-help
EXTERNAL = external-link
DOWNLOAD = download
UPLOAD = upload
ROCKET = rocket
HOME = house
ARROW_LEFT = arrow-left
ARROW_UP = arrow-up
ARROW_RIGHT = arrow-right
CHEVRON_RIGHT = chevron-right
CHEVRON_DOWN = chevron-down
CLOCK = clock
PALETTE = palette
MENU = menu
MONITOR = monitor
KEYBOARD = keyboard
SUN = sun
MOON = moon
EYE = eye
EYE_OFF = eye-off
LINK = link
TAG = tag
LOCK = lock
SCAN = scan-search
HARD_DRIVE = hard-drive
PACKAGE = package
SPARKLES = sparkles
ZOOM_IN = zoom-in
ZOOM_OUT = zoom-out
MAXIMIZE = maximize
LAYOUT = layout-dashboard
# --- Docks et outils
DOCK_SCENE = list-tree
DOCK_FILES = folder-tree
DOCK_INSPECTOR = sliders-horizontal
DOCK_OUTPUT = terminal
DOCK_ANIMATION = film
DOCK_SCRIPT = code
DOCK_GAME = gamepad-2
VIEW_2D = square-dashed
VIEW_3D = box
TOOL_MOVE = move
TOOL_ROTATE = rotate-cw
TOOL_SCALE = scaling
GRID = grid-2x2
MAGNET = magnet
FOCUS = locate-fixed
GLOBE = globe
SIGNAL = radio
GROUPS = tags
# --- Fichiers
FOLDER = folder
FOLDER_OPEN = folder-open
FOLDER_PLUS = folder-plus
FILE = file
FILE_PLUS = file-plus
FILE_TEXT = file-text
FILE_SCENE = clapperboard
FILE_SCRIPT = scroll-text
FILE_IMAGE = image
FILE_AUDIO = music
FILE_MESH = box
FILE_FONT = type
# --- Nœuds
NODE = circle
NODE_2D = circle-dot
NODE_3D = axis-3d
NODE_SPRITE = image
NODE_ANIMATED_SPRITE = images
NODE_ANIMATION = film
NODE_AREA = scan
NODE_AUDIO = volume-2
NODE_HBOX = columns-3
NODE_VBOX = rows-3
NODE_BUTTON = mouse-pointer-click
NODE_CAMERA = video
NODE_LAYER = layers
NODE_CENTER = square-dot
NODE_CHARACTER = person-standing
NODE_SHAPE = shapes
NODE_COLOR_RECT = paint-bucket
NODE_CONTAINER = layout-grid
NODE_CONTROL = app-window
NODE_SUN = sun
NODE_LABEL = type
NODE_LINE_EDIT = text-cursor-input
NODE_MARGIN = frame
NODE_MARKER = crosshair
NODE_MESH = box
NODE_OMNI = lightbulb
NODE_PANEL = panel-top
NODE_PROGRESS = gauge
NODE_RAY = move-down
NODE_RIGID = weight
NODE_SPOT = flashlight
NODE_STATIC = brick-wall
NODE_TILEMAP = grid-3x3
NODE_TIMER = timer
NODE_WORLD = earth
"""


def main():
    with open(os.path.join(LUCIDE, 'codepoints.json'), encoding='utf-8') as f:
        codes = json.load(f)
    icones = []
    for ligne in ICONES.splitlines():
        ligne = ligne.strip()
        if not ligne or ligne.startswith('#'):
            continue
        nom, lucide = [x.strip() for x in ligne.split('=')]
        if lucide not in codes:
            raise SystemExit("Icône inconnue dans Lucide : %s" % lucide)
        icones.append((nom, lucide, codes[lucide]))

    options = subset.Options()
    options.layout_features = []
    options.name_IDs = [1, 2]
    options.notdef_outline = False
    options.glyph_names = False
    options.hinting = False
    police = TTFont(os.path.join(LUCIDE, 'lucide.ttf'))
    extracteur = subset.Subsetter(options)
    extracteur.populate(unicodes=sorted(set(c for _, _, c in icones)))
    extracteur.subset(police)
    tampon = io.BytesIO()
    police.save(tampon)
    donnees = tampon.getvalue()

    lignes = [
        "// Fichier généré par editor/icons/generer_icones.py : ne pas modifier à la main.",
        "// Icônes de l'éditeur, extraites de la police Lucide (licence ISC, thirdparty/lucide).",
        "#pragma once",
        "",
        "// Chaque icône est un caractère de la police de l'interface, à insérer dans un texte :",
        "//     ImGui::Button(ICON_PLAY \" Jouer\")",
    ]
    for nom, lucide, code in icones:
        utf8 = ''.join('\\x%02x' % b for b in chr(code).encode('utf-8'))
        lignes.append('#define ICON_%s "%s" // %s' % (nom, utf8, lucide))
    lignes += ["", "#ifdef MEX_EDITOR_ICONS_DATA", "static const unsigned char EDITOR_ICONS_TTF[] = {"]
    for i in range(0, len(donnees), 24):
        lignes.append('\t' + ', '.join('0x%02x' % b for b in donnees[i:i + 24]) + ',')
    lignes += ["};", "static const unsigned int EDITOR_ICONS_TTF_SIZE = %d;" % len(donnees), "#endif", ""]
    sortie = os.path.join(RACINE, 'editor', 'editor_icons.gen.h')
    with open(sortie, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lignes))
    print('%d icônes, %d octets -> %s' % (len(icones), len(donnees), sortie))


if __name__ == '__main__':
    main()
