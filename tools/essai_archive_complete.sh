#!/usr/bin/env bash
# Vérifie l'archive complète pour Linux : décompressée dans un dossier vide, sans modèles
# installés sur la machine, son éditeur doit trouver les modèles du dossier « templates »,
# exporter le jeu d'exemple, puis le jeu Linux exporté doit tourner.
#
#   tools/essai_archive_complete.sh <mex-...-linux-x86_64.tar.gz> [web] [windows] [macos]
set -euo pipefail

if [ $# -lt 1 ]; then
	echo "Usage : $0 <archive complète pour Linux> [web] [windows] [macos]" >&2
	exit 2
fi
systemes="${*:2}"
archive=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
racine=$(cd "$(dirname "$0")/.." && pwd)
essai=$(mktemp -d)
trap 'rm -rf "$essai"' EXIT

# Un dossier personnel vide : aucun modèle installé ne peut aider l'éditeur.
mkdir -p "$essai/maison"
export HOME="$essai/maison" XDG_DATA_HOME="$essai/maison/donnees" XDG_CONFIG_HOME="$essai/maison/config"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

tar -xzf "$archive" -C "$essai"
dossier=$(find "$essai" -mindepth 1 -maxdepth 1 -type d -name 'mex-*' | head -n 1)
if [ -z "$dossier" ] || [ ! -x "$dossier/mex" ]; then
	echo "::error::L'archive ne contient pas l'éditeur « mex » exécutable."
	exit 1
fi
for fichier in mex-jeu LISEZMOI.txt templates/version.txt templates/linux-x86_64/mex-jeu; do
	if [ ! -f "$dossier/$fichier" ]; then
		echo "::error::Fichier absent de l'archive : $fichier"
		exit 1
	fi
done
echo "Contenu de l'archive :"
(cd "$dossier" && find . -type f | sort)

cp -r "$racine/projects/plateforme" "$essai/jeu"
numero=0
prereglage() {
	cat >> "$essai/jeu/export_presets.cfg" <<PRESETS
[preset.$numero]

name="$1"
platform="$1"
export_path="$2"

[preset.$numero.options]

PRESETS
	numero=$((numero + 1))
}
: > "$essai/jeu/export_presets.cfg"
prereglage "Linux" "export/linux/jeu.x86_64"
attendus=("export/linux/jeu.x86_64")
for systeme in $systemes; do
	case "$systeme" in
		web) prereglage "Web" "export/web/jeu.html"; attendus+=("export/web/jeu.html") ;;
		windows) prereglage "Windows Desktop" "export/windows/jeu.exe"; attendus+=("export/windows/jeu.exe") ;;
		macos) prereglage "macOS" "export/macos/jeu"; attendus+=("export/macos/jeu.app/Contents/Info.plist") ;;
		*) echo "Système inconnu : $systeme" >&2; exit 2 ;;
	esac
done

"$dossier/mex" --version
(cd "$essai/jeu" && "$dossier/mex" --path . --export "*") | tee "$essai/export.log"
if grep -q "ERREUR" "$essai/export.log"; then
	echo "::error::L'export depuis l'archive complète a échoué."
	exit 1
fi
for fichier in "${attendus[@]}"; do
	if [ ! -s "$essai/jeu/$fichier" ]; then
		echo "::error::Fichier exporté absent : $fichier"
		exit 1
	fi
done

# Le jeu Linux exporté, déplacé seul ailleurs, tourne sans fenêtre.
mkdir -p "$essai/ailleurs"
cp "$essai/jeu/export/linux/"* "$essai/ailleurs/"
chmod +x "$essai/ailleurs/jeu.x86_64"
(cd "$essai/ailleurs" && timeout 60 ./jeu.x86_64 --quit-after 30)
echo "Archive complète : export et lancement réussis."
