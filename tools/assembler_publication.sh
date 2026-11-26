#!/usr/bin/env bash
# Assemble les fichiers publiés à partir des exécutables compilés pour chaque système.
#
#   tools/assembler_publication.sh <dossier des binaires> <libellé> <dossier de sortie>
#
# Le dossier des binaires contient un sous-dossier par système :
#   binaires-linux-x86_64/{mex, mex-jeu}
#   binaires-windows-x86_64/{mex.exe, mex-jeu.exe, mex.console.exe}
#   binaires-macos-universal/{mex, mex-jeu}
#   binaires-web-wasm32/mex-jeu.js
# Un système absent (compilation échouée) est simplement laissé de côté ; seul l'éditeur
# Linux est indispensable, car c'est lui qui assemble les fichiers.
#
# Fichiers produits :
#   mex-<libellé>-windows-x86_64.exe          l'éditeur en un seul fichier, modèles incorporés
#   mex-<libellé>-windows-x86_64.console.exe  sa version console (à garder à côté du premier)
#   mex-<libellé>-linux-x86_64                l'éditeur en un seul fichier, modèles incorporés
#   mex-<libellé>-windows-x86_64.zip          archive complète pour Windows
#   mex-<libellé>-linux-x86_64.tar.gz         archive complète pour Linux
#   mex-<libellé>-macos-universal.tar.gz      archive complète pour macOS (un exécutable signé
#                                             ne peut rien recevoir à sa suite : pas de
#                                             fichier unique pour ce système)
#   mex-<libellé>-web-wasm32.zip              le modèle d'export web seul
#
# Une archive complète contient tout ce que la compilation a produit pour ce système
# (éditeur, version console, lanceur de jeu mex-jeu) et, dans « templates », les modèles
# d'export de TOUS les systèmes : l'éditeur les trouve là, sans installation.
#
#   mex-<libellé>-modeles-export.tpz          le paquet des modèles d'export (archive ZIP), pour
#                                             un éditeur sans modèles incorporés :
#                                             « Projet > Modèles d'exportation... »
#   mex-<libellé>-exemples.zip                les projets d'exemple
set -euo pipefail

if [ $# -ne 3 ]; then
	echo "Usage : $0 <dossier des binaires> <libellé> <dossier de sortie>" >&2
	exit 2
fi
binaires=$1
libelle=$2
sortie=$3
racine=$(cd "$(dirname "$0")/.." && pwd)

linux="$binaires/binaires-linux-x86_64"
windows="$binaires/binaires-windows-x86_64"
macos="$binaires/binaires-macos-universal"
web="$binaires/binaires-web-wasm32"

if [ ! -f "$linux/mex" ]; then
	echo "::error::L'éditeur Linux manque : c'est lui qui assemble les fichiers publiés."
	exit 1
fi
# Le transport des artefacts ne garde pas le droit d'exécution.
chmod +x "$linux/mex"
[ -f "$linux/mex-jeu" ] && chmod +x "$linux/mex-jeu"
[ -f "$macos/mex" ] && chmod +x "$macos/mex"
[ -f "$macos/mex-jeu" ] && chmod +x "$macos/mex-jeu"

# Modèles d'export disponibles : <identifiant>=<fichier>.
modeles=()
[ -f "$windows/mex-jeu.exe" ] && modeles+=("windows-x86_64=$windows/mex-jeu.exe")
[ -f "$linux/mex-jeu" ] && modeles+=("linux-x86_64=$linux/mex-jeu")
[ -f "$macos/mex-jeu" ] && modeles+=("macos-universal=$macos/mex-jeu")
[ -f "$web/mex-jeu.js" ] && modeles+=("web-wasm32=$web/mex-jeu.js")
if [ ${#modeles[@]} -eq 0 ]; then
	echo "::error::Aucun modèle d'export disponible."
	exit 1
fi
echo "Modèles incorporés : ${modeles[*]}"

mkdir -p "$sortie"

# Paquet des modèles d'export : templates/version.txt et templates/<système>/<fichier>.
travail_modeles=$(mktemp -d)
mkdir -p "$travail_modeles/templates"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$linux/mex" --version | tail -n 1 > "$travail_modeles/templates/version.txt"
for modele in "${modeles[@]}"; do
	identifiant=${modele%%=*}
	fichier=${modele#*=}
	mkdir -p "$travail_modeles/templates/$identifiant"
	cp "$fichier" "$travail_modeles/templates/$identifiant/"
done
rm -f "$sortie/mex-$libelle-modeles-export.tpz"
sortie_absolue=$(cd "$sortie" && pwd)
(cd "$travail_modeles" && zip -q -r -9 "$sortie_absolue/mex-$libelle-modeles-export.tpz" templates)
rm -rf "$travail_modeles"

# Linux : un seul fichier.
"$linux/mex" --incorporer-modeles "$sortie/mex-$libelle-linux-x86_64" "${modeles[@]}"
chmod +x "$sortie/mex-$libelle-linux-x86_64"

# Windows : un seul .exe, et sa version console.
if [ -f "$windows/mex.exe" ]; then
	"$linux/mex" --incorporer-modeles "$sortie/mex-$libelle-windows-x86_64.exe" "editeur=$windows/mex.exe" "${modeles[@]}"
	if [ -f "$windows/mex.console.exe" ]; then
		cp "$windows/mex.console.exe" "$sortie/mex-$libelle-windows-x86_64.console.exe"
	fi
fi

# Archives complètes : pour chaque système, tout ce que sa compilation a produit, plus les
# modèles d'export de tous les systèmes dans « templates ».
#   archive_complete <système> <dossier des binaires> <zip|tar>
archive_complete() {
	local systeme=$1 source=$2 format=$3
	local paquet="mex-$libelle-$systeme"
	local travail
	travail=$(mktemp -d)
	mkdir -p "$travail/$paquet/templates"
	# Les exécutables du système : mex, mex-jeu, mex.console.exe...
	cp "$source"/mex* "$travail/$paquet/"
	local modele identifiant fichier
	for modele in "${modeles[@]}"; do
		identifiant=${modele%%=*}
		fichier=${modele#*=}
		mkdir -p "$travail/$paquet/templates/$identifiant"
		cp "$fichier" "$travail/$paquet/templates/$identifiant/"
	done
	cp "$version_modeles" "$travail/$paquet/templates/version.txt"
	chmod 644 "$travail/$paquet/templates/version.txt"
	{
		echo "MEX $(cat "$version_modeles") - $systeme"
		echo
		echo "Lancez « mex » : c'est l'éditeur. « mex-jeu » lance un jeu sans l'éditeur"
		echo "(mex-jeu --path <dossier du projet>)."
		echo
		echo "Le dossier « templates » contient les modèles d'export de tous les systèmes :"
		for modele in "${modeles[@]}"; do
			echo "  - ${modele%%=*}"
		done
		echo "Gardez-le à côté de l'éditeur : « Projet > Exporter le jeu... » s'en sert."
		echo
		echo "Licences des composants utilisés : mex --licences"
	} > "$travail/$paquet/LISEZMOI.txt"
	rm -f "$sortie_absolue/$paquet.zip" "$sortie_absolue/$paquet.tar.gz"
	if [ "$format" = zip ]; then
		(cd "$travail" && zip -q -r -9 "$sortie_absolue/$paquet.zip" "$paquet")
	else
		tar -czf "$sortie_absolue/$paquet.tar.gz" -C "$travail" "$paquet"
	fi
	rm -rf "$travail"
}
version_modeles=$(mktemp)
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$linux/mex" --version | tail -n 1 > "$version_modeles"
[ -f "$windows/mex.exe" ] && archive_complete windows-x86_64 "$windows" zip
archive_complete linux-x86_64 "$linux" tar
# macOS : l'éditeur est signé, on ne peut rien lui ajouter ; l'archive est sa seule forme.
[ -f "$macos/mex" ] && archive_complete macos-universal "$macos" tar
rm -f "$version_modeles"

# Web : ce n'est pas un éditeur, seulement le modèle d'export.
if [ -f "$web/mex-jeu.js" ]; then
	rm -f "$sortie_absolue/mex-$libelle-web-wasm32.zip"
	(cd "$web" && zip -q -9 "$sortie_absolue/mex-$libelle-web-wasm32.zip" mex-jeu.js)
fi

# Projets d'exemple, à part : l'éditeur n'en a pas besoin pour fonctionner.
if [ -d "$racine/projects" ]; then
	exemples="mex-$libelle-exemples"
	travail=$(mktemp -d)
	mkdir -p "$travail/$exemples"
	cp -r "$racine/projects/." "$travail/$exemples/"
	(cd "$travail" && zip -qr "$exemples.zip" "$exemples")
	mv "$travail/$exemples.zip" "$sortie/"
	rm -rf "$travail"
fi

ls -l "$sortie"
