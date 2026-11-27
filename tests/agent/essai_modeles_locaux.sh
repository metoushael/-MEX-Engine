#!/bin/bash
# Essai de MEX Agent, éditeur compris, contre le faux service (tests/agent/faux_service.py) :
# adresses saisies de toutes les façons, modèles locaux sans outils (gabarit de Gemma), refus
# du serveur, pannes HTTP, et le scénario complet avec outils dans les deux formats.
#
#     tests/agent/essai_modeles_locaux.sh <chemin de mex> [port]
#
# Rien n'est lu ni écrit dans les réglages de l'utilisateur : l'éditeur travaille dans un
# dossier temporaire. Sans écran (variable DISPLAY vide), « xvfb-run » est utilisé.
# Code de sortie : 0 si tout passe.
[ -n "$1" ] || { echo "Usage : $0 <chemin de mex> [port]"; exit 2; }
mex="$(readlink -f "$1")"
port="${2:-8791}"
racine="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
service=""
echecs=0
reussites=0

fin() {
	[ -n "$service" ] && kill "$service" 2>/dev/null
	rm -rf "$tmp"
}
trap fin EXIT

verifier() { # <libellé> <commande...>
	local nom="$1"; shift
	if "$@" > /dev/null 2>&1; then
		reussites=$((reussites + 1))
	else
		echecs=$((echecs + 1))
		echo "  ECHEC : $nom"
	fi
}
contient() { grep -qaF -- "$2" "$1"; }
absent() { ! grep -qaF -- "$2" "$1"; }
compte() { [ "$(grep -caF -- "$2" "$1")" = "$3" ]; }

# essai <nom> <genre du faux serveur> <fournisseur> <adresse> <modèle> <images> <lignes du scénario>
essai() {
	local nom="$1" genre="$2" fournisseur="$3" adresse="$4" modele="$5" images="$6" scenario="$7"
	echo "--- $nom"
	[ -n "$service" ] && kill "$service" 2>/dev/null && wait "$service" 2>/dev/null
	service=""
	journal="$tmp/$nom.service.log"
	echanges="$tmp/$nom.echanges.log"
	: > "$journal"; : > "$echanges"
	if [ "$genre" != "aucun" ]; then
		python3 "$racine/tests/agent/faux_service.py" "$port" "$journal" "$genre" &
		service=$!
		for _ in $(seq 50); do
			curl -s -o /dev/null "http://127.0.0.1:$port/v1/models" && break
			sleep 0.1
		done
		: > "$journal"
	fi
	rm -rf "$tmp/projet" "$tmp/donnees"
	cp -r "$racine/projects/plateforme" "$tmp/projet"
	mkdir -p "$tmp/donnees"
	printf '5 agent_attendre\n%s\n' "$scenario" > "$tmp/scenario.txt"
	local lanceur=()
	[ -z "$DISPLAY" ] && lanceur=(xvfb-run -a -s "-screen 0 1440x860x24")
	env XDG_DATA_HOME="$tmp/donnees" SDL_AUDIODRIVER=dummy MEX_EDITOR_TEST_SCRIPT="$tmp/scenario.txt" \
		MEX_AGENT_PROVIDER="$fournisseur" MEX_AGENT_URL="$adresse" MEX_AGENT_MODEL="$modele" MEX_AGENT_AUTO=1 \
		MEX_AGENT_ANIMATIONS=0 MEX_AGENT_JOURNAL="$echanges" ANTHROPIC_API_KEY=cle-de-test OPENAI_API_KEY=cle-de-test GEMINI_API_KEY=cle-de-test \
		timeout 300 "${lanceur[@]}" "$mex" --editor --path "$tmp/projet" --quit-after "$images" > "$tmp/$nom.editeur.log" 2>&1
	verifier "l'éditeur s'est terminé normalement" [ $? -eq 0 ]
	if [ -n "$MEX_ESSAI_GARDER" ]; then
		mkdir -p "$MEX_ESSAI_GARDER" && cp "$journal" "$echanges" "$MEX_ESSAI_GARDER/"
	fi
}

base="http://127.0.0.1:$port"
trois_demandes=$'40 agent Bonjour\n60 agent Deuxième demande\n80 agent Troisième demande\n100 agent_modeles'
sans_outils() { # vérifications communes aux modèles sans outils : <nombre de réponses>
	verifier "$1 conversation(s) acceptée(s) par le gabarit" compte "$journal" "CONVERSATION CONFORME" "$1"
	verifier "aucune requête mal formée" absent "$journal" "NON CONFORME"
	verifier "POST /v1/chat/completions" contient "$journal" "POST /v1/chat/completions"
	verifier "GET /v1/models" contient "$journal" "GET /v1/models"
	verifier "aucun chemin inattendu hors des questions au serveur" bash -c "! grep -a 'CHEMIN INATTENDU' '$journal' | grep -qav ' /api/'"
	verifier "l'utilisateur est prévenu une seule fois" compte "$echanges" "ne sait pas appeler d'outils" 1
	[ "$1" = 3 ] && verifier "aucun avis d'erreur" bash -c "[ \"\$(grep -ac '^\*\*\* ' '$echanges')\" = 1 ]"
}

essai "lmstudio-sans-v1" lmstudio local "$base" faux-gemma 140 "$trois_demandes"
sans_outils 3
verifier "LM Studio interrogé sur le modèle" contient "$journal" "GET /api/v1/models"
verifier "aucun refus du serveur" absent "$journal" "REFUS"
verifier "origine : LM Studio" contient "$echanges" "(d'après LM Studio)"
verifier "dernière requête : system user assistant user assistant user" contient "$journal" "(rôles : system user assistant user assistant user)"

essai "lmstudio-ancien-v1-barre" lmstudio-ancien local "$base/v1/" faux-gemma 140 "$trois_demandes"
sans_outils 3
verifier "repli sur /api/v0/models" contient "$journal" "GET /api/v0/models"
verifier "aucun refus du serveur" absent "$journal" "REFUS"

essai "lmstudio-v1-double" lmstudio local "$base/v1/v1" faux-gemma 140 "$trois_demandes"
sans_outils 3

essai "lmstudio-adresse-de-la-liste" lmstudio local "$base/v1/models" faux-gemma 140 "$trois_demandes"
sans_outils 3

essai "ollama-adresse-complete" ollama local "$base/v1/chat/completions" faux-gemma 140 "$trois_demandes"
sans_outils 3
verifier "Ollama interrogé sur le modèle" contient "$journal" "POST /api/show  modèle=faux-gemma"
verifier "aucun refus du serveur" absent "$journal" "REFUS"
verifier "origine : Ollama" contient "$echanges" "(d'après Ollama)"

essai "ollama-api-chat" ollama local "$base/api/chat" faux-gemma 140 "$trois_demandes"
sans_outils 3

essai "serveur-inconnu-refus-des-outils" inconnu local "$base" faux-gemma 140 "$trois_demandes"
sans_outils 3
verifier "un seul refus, puis la conversation continue sans outils" compte "$journal" "REFUS outils" 1
verifier "origine : le refus du service" contient "$echanges" "(le service l'a indiqué)"

essai "gabarit-strict" inconnu local "$base" faux-strict 140 "$trois_demandes"
sans_outils 3
verifier "refus du gabarit avec outils, une fois" compte "$journal" "REFUS gabarit (outils envoyés)" 1
verifier "refus du rôle system, une fois" compte "$journal" "REFUS gabarit : Only user and assistant roles" 1
verifier "dernière requête : user assistant user assistant user" contient "$journal" "(rôles : user assistant user assistant user)"

essai "service-compatible-sans-outils" inconnu compatible "$base/v1" faux-gemma 140 "$trois_demandes"
sans_outils 3

essai "pannes" inconnu local "$base" faux-panne 160 $'40 agent Un\n60 agent Deux\n80 agent Trois\n100 agent Quatre\n120 agent_modeles'
sans_outils 1
verifier "page HTML 502 expliquée" contient "$echanges" "*** Le service a refusé la requête (HTTP 502) : page web « 502 Bad Gateway »"
verifier "texte non JSON expliqué" contient "$echanges" "*** Le service n'a pas répondu comme prévu : réponse illisible"
verifier "réponse vide expliquée" contient "$echanges" "*** Le modèle a renvoyé une réponse vide"
verifier "après trois pannes, les quatre demandes font un seul message user" contient "$journal" "(rôles : system user)"

essai "mauvais-chemin" lmstudio local "$base/mauvais/v1" faux-gemma 100 $'40 agent Bonjour\n60 agent_modeles'
verifier "chemin inattendu signalé" contient "$echanges" "Unexpected endpoint or method. (POST /mauvais/v1/chat/completions)"
verifier "adresse appelée montrée" contient "$echanges" "Adresse appelée : $base/mauvais/v1/chat/completions"
verifier "liste : chemin inattendu signalé" contient "$echanges" "*** Impossible de lister les modèles : Unexpected endpoint or method. (GET /mauvais/v1/models)"

essai "serveur-arrete" aucun local "http://127.0.0.1:9" faux-gemma 100 $'40 agent Bonjour\n60 agent Encore'
verifier "connexion impossible expliquée" contient "$echanges" "*** Connexion impossible à http://127.0.0.1:9/v1/chat/completions"
verifier "le serveur est interrogé de nouveau à la demande suivante" compte "$echanges" ">>> GET http://127.0.0.1:9/api/v1/models" 2
verifier "conseil pour LM Studio et Ollama" contient "$echanges" "Vérifiez que le serveur local est démarré"

avec_outils() {
	verifier "13 contrôles du scénario réussis" compte "$journal" "CONTROLE OK" 13
	verifier "aucun contrôle en échec" absent "$journal" "CONTROLE ECHEC"
	verifier "scénario terminé" contient "$journal" "SCENARIO TERMINE"
	verifier "toutes les requêtes sont conformes" absent "$journal" "NON CONFORME"
	verifier "cinq requêtes conformes" compte "$journal" "REQUETE CONFORME" 5
	[ "$1" = "un-refus" ] || verifier "aucun refus" absent "$journal" "REFUS"
	verifier "aucun avis d'erreur" absent "$echanges" "*** Le service"
}
scenario_outils=$'40 agent Fais ton essai complet\n60 agent_modeles'

essai "outils-lmstudio-sans-v1" lmstudio local "$base" faux-outils 200 "$scenario_outils"
avec_outils
verifier "POST /v1/chat/completions" contient "$journal" "POST /v1/chat/completions"
verifier "pas d'avis « sans outils »" absent "$echanges" "ne sait pas appeler d'outils"

essai "outils-ollama" ollama local "$base/v1" faux-outils 200 "$scenario_outils"
avec_outils

essai "outils-serveur-inconnu" inconnu local "$base/v1/" faux-outils 200 "$scenario_outils"
avec_outils

essai "outils-openai" inconnu openai "$base/v1" faux-outils 200 "$scenario_outils"
avec_outils

# Gemini 3 : chaque étape d'outils revient avec sa « signature de réflexion ».
essai "outils-gemini" inconnu gemini "$base/v1" faux-gemini 200 "$scenario_outils"
avec_outils
verifier "aucune requête refusée pour signature manquante" absent "$journal" "REFUS signature"
verifier "les signatures reviennent à chaque étape" compte "$journal" "SIGNATURES CONFORMES" 4
verifier "la signature de la première étape est renvoyée" contient "$echanges" '"thought_signature":"sig-0"'

# Gemini derrière un relais qui perd les signatures : un refus, puis la valeur de remplacement.
essai "outils-gemini-relais" inconnu compatible "$base/v1" faux-gemini-relais 200 "$scenario_outils"
avec_outils un-refus
verifier "un seul refus, puis la requête repart" compte "$journal" "REFUS signature" 1
verifier "valeur de remplacement envoyée" contient "$echanges" '"thought_signature":"skip_thought_signature_validator"'
verifier "l'erreur de Google n'est pas montrée à l'utilisateur" absent "$echanges" "*** Le service a refusé"

essai "outils-anthropic" inconnu anthropic "$base/v1" claude-essai 200 "$scenario_outils"
avec_outils
verifier "POST /v1/messages" contient "$journal" "POST /v1/messages"

echo "BILAN MODELES LOCAUX : $reussites vérifications réussies, $echecs échec(s)"
[ "$echecs" -eq 0 ]
