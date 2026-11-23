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
#   mex-<libellé>-windows-x86_64.exe          l'éditeur seul, avec tous les modèles d'export
#   mex-<libellé>-windows-x86_64.console.exe  sa version console (à garder à côté du premier)
#   mex-<libellé>-linux-x86_64                l'éditeur seul, avec tous les modèles d'export
#   mex-<libellé>-macos-universal.tar.gz      l'éditeur et les modèles (un exécutable signé
#                                             ne peut rien recevoir à sa suite)
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

# macOS : l'éditeur est signé, on ne peut rien lui ajouter. Une petite archive : l'éditeur,
# son modèle, et ceux des autres systèmes dans « templates ».
if [ -f "$macos/mex" ]; then
	paquet="mex-$libelle-macos-universal"
	travail=$(mktemp -d)
	mkdir -p "$travail/$paquet"
	cp "$macos/mex" "$travail/$paquet/"
	[ -f "$macos/mex-jeu" ] && cp "$macos/mex-jeu" "$travail/$paquet/"
	if [ -f "$windows/mex-jeu.exe" ]; then
		mkdir -p "$travail/$paquet/templates/windows-x86_64"
		cp "$windows/mex-jeu.exe" "$travail/$paquet/templates/windows-x86_64/"
	fi
	if [ -f "$linux/mex-jeu" ]; then
		mkdir -p "$travail/$paquet/templates/linux-x86_64"
		cp "$linux/mex-jeu" "$travail/$paquet/templates/linux-x86_64/"
	fi
	if [ -f "$web/mex-jeu.js" ]; then
		mkdir -p "$travail/$paquet/templates/web-wasm32"
		cp "$web/mex-jeu.js" "$travail/$paquet/templates/web-wasm32/"
	fi
	tar -czf "$sortie/$paquet.tar.gz" -C "$travail" "$paquet"
	rm -rf "$travail"
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
