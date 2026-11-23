#!/usr/bin/env bash
# Vérifie l'éditeur distribué en un seul fichier : seul dans un dossier vide, il doit exporter
# le jeu d'exemple, puis le jeu Linux exporté, déplacé seul ailleurs, doit tourner.
#
#   tools/essai_fichier_unique.sh <éditeur Linux en un seul fichier> [web] [windows]
#
# « web » et « windows » : exporte aussi pour ces systèmes (leurs modèles doivent être
# incorporés à l'éditeur).
set -euo pipefail

if [ $# -lt 1 ]; then
	echo "Usage : $0 <éditeur Linux en un seul fichier> [web] [windows]" >&2
	exit 2
fi
systemes="${*:2}"
editeur=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
racine=$(cd "$(dirname "$0")/.." && pwd)
essai=$(mktemp -d)
trap 'rm -rf "$essai"' EXIT

mkdir -p "$essai/outil"
cp "$editeur" "$essai/outil/mex"
chmod +x "$essai/outil/mex"
cp -r "$racine/projects/plateforme" "$essai/jeu"
# Préréglages d'export : Linux toujours, les autres systèmes s'ils sont demandés.
numero=0
prereglage() {
	cat >> "$essai/jeu/export_presets.cfg" <<PRESETS
[preset.$numero]

name="$1"
platform="$1"
export_path="$2"

[preset.$numero.options]

binary_format/embed_pck=true

PRESETS
	numero=$((numero + 1))
}
prereglage "Linux" "export/linux/plateforme.x86_64"
case " $systemes " in *" web "*) prereglage "Web" "export/web/plateforme.html" ;; esac
case " $systemes " in *" windows "*) prereglage "Windows Desktop" "export/windows/plateforme.exe" ;; esac

export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
# Dossier de préférences neuf : les modèles sont extraits de l'éditeur, pas retrouvés ailleurs.
export XDG_DATA_HOME="$essai/donnees"
"$essai/outil/mex" --version
(cd "$essai/jeu" && "$essai/outil/mex" --path . --export "*") | tee "$essai/export.log"
if grep -q "ERREUR" "$essai/export.log"; then
	echo "::error::L'export depuis l'éditeur en un seul fichier a échoué."
	exit 1
fi
test -s "$essai/jeu/export/linux/plateforme.x86_64"
case " $systemes " in *" web "*)
	test -s "$essai/jeu/export/web/plateforme.html"
	grep -q 'id="archive"' "$essai/jeu/export/web/plateforme.html"
	;;
esac
case " $systemes " in *" windows "*)
	test -s "$essai/jeu/export/windows/plateforme.exe"
	;;
esac
# Le jeu exporté est déplacé seul dans un autre dossier, puis lancé.
mkdir -p "$essai/ailleurs"
mv "$essai/jeu/export/linux/plateforme.x86_64" "$essai/ailleurs/"
(cd "$essai/ailleurs" && ./plateforme.x86_64 --quit-after 60) > "$essai/jeu.log" 2>&1 || { cat "$essai/jeu.log"; exit 1; }
cat "$essai/jeu.log"
if grep -q "ERREUR" "$essai/jeu.log"; then
	echo "::error::Le jeu exporté a signalé une erreur."
	exit 1
fi
echo "Éditeur en un seul fichier : export et lancement réussis."
