#!/bin/bash
# Essai de MEX Agent contre un VRAI serveur local (LM Studio, Ollama, llama.cpp...) : l'éditeur
# envoie les demandes données, puis ce script résume chaque échange (adresse appelée, rôles
# envoyés, outils, code HTTP, réponse ou erreur) et les avis affichés à l'utilisateur.
#
#     tests/agent/essai_serveur_reel.sh <mex> <adresse> <modèle> [demande ...]
#     tests/agent/essai_serveur_reel.sh build/bin/mex http://localhost:1234 google/gemma-3-4b
#     tests/agent/essai_serveur_reel.sh build/bin/mex http://localhost:11434 qwen3:4b "Liste les fichiers du projet"
#
# Sans demande : trois messages de conversation. Le projet d'exemple est copié dans un dossier
# temporaire et les réglages de l'utilisateur ne sont ni lus ni modifiés. Le journal complet
# des échanges reste dans le fichier indiqué à la fin (variable MEX_ESSAI_JOURNAL pour le choisir).
[ -n "$3" ] || { echo "Usage : $0 <chemin de mex> <adresse> <modèle> [demande ...]"; exit 2; }
mex="$(readlink -f "$1")"; adresse="$2"; modele="$3"; shift 3
racine="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
journal="${MEX_ESSAI_JOURNAL:-$tmp/echanges.log}"
: > "$journal"
[ $# -eq 0 ] && set -- "Bonjour ! Qui es-tu ? Réponds en une phrase." "Écris un script MexScript de trois lignes qui affiche bonjour." "Merci. Résume notre échange en une phrase."
cp -r "$racine/projects/plateforme" "$tmp/projet"
mkdir -p "$tmp/donnees"
{
	echo "5 agent_attendre"
	image=40
	for demande in "$@"; do
		echo "$image agent $demande"
		image=$((image + 20))
	done
	echo "$image agent_modeles"
} > "$tmp/scenario.txt"
lanceur=()
[ -z "$DISPLAY" ] && lanceur=(xvfb-run -a -s "-screen 0 1440x860x24")
env XDG_DATA_HOME="$tmp/donnees" SDL_AUDIODRIVER=dummy MEX_EDITOR_TEST_SCRIPT="$tmp/scenario.txt" \
	MEX_AGENT_PROVIDER="${MEX_ESSAI_FOURNISSEUR:-local}" MEX_AGENT_URL="$adresse" MEX_AGENT_MODEL="$modele" MEX_AGENT_AUTO=1 \
	MEX_AGENT_ANIMATIONS=0 MEX_AGENT_JOURNAL="$journal" \
	timeout "${MEX_ESSAI_DUREE:-1500}" "${lanceur[@]}" "$mex" --editor --path "$tmp/projet" --quit-after $((image + 40)) > "$tmp/editeur.log" 2>&1
code=$?
python3 - "$journal" <<'PY'
import json, re, sys
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
blocks = re.split(r"(?m)^(?=>>> |<<< |\*\*\* )", text)
errors = 0
answers = 0
for block in blocks:
    block = block.rstrip("\n")
    if not block:
        continue
    head, _, rest = block.partition("\n")
    if head.startswith(">>> "):
        line = head
        try:
            body = json.loads(rest)
            roles = " ".join(m.get("role", "?") for m in body.get("messages", []))
            if "messages" in body:
                line += "\n      rôles : %s | outils : %d" % (roles, len(body.get("tools", [])))
        except ValueError:
            pass
        print(line)
    elif head.startswith("<<< "):
        body, _, status = rest.rpartition("\n")
        summary = body.strip().replace("\n", " ")[:300]
        try:
            data = json.loads(body)
            if isinstance(data, dict) and data.get("choices"):
                message = data["choices"][0].get("message", {})
                calls = [c.get("function", {}).get("name", "?") + "(" + str(c.get("function", {}).get("arguments", ""))[:80] + ")" for c in message.get("tool_calls") or []]
                content = (message.get("content") or "").strip().replace("\n", " ")
                summary = ("appels d'outils : " + ", ".join(calls) + " | " if calls else "") + "texte : " + (content[:300] if content else "(aucun)")
                answers += 1
            elif isinstance(data, dict) and "error" in data:
                summary = "ERREUR : " + json.dumps(data["error"], ensure_ascii=False)[:400]
                errors += 1
            elif isinstance(data, dict) and ("data" in data or "models" in data):
                items = data.get("data") or data.get("models") or []
                summary = "%d modèle(s) : %s" % (len(items), ", ".join(str(i.get("id") or i.get("key") or i.get("name")) for i in items if isinstance(i, dict))[:300])
            elif isinstance(data, dict) and "capabilities" in data:
                summary = "capacités : %s" % data["capabilities"]
        except ValueError:
            pass
        print("    <<< HTTP %s : %s" % (status.strip() or "?", summary))
    else:
        print("    AVIS : " + block[4:].replace("\n", "\n           "))
print("BILAN SERVEUR REEL : %d réponse(s) du modèle, %d erreur(s) renvoyée(s) par le serveur" % (answers, errors))
PY
echo "Éditeur : code de sortie $code. Journal complet : $journal"
[ -z "$MEX_ESSAI_JOURNAL" ] && cp "$journal" ./mex-agent-echanges.log 2>/dev/null && echo "Copie : ./mex-agent-echanges.log"
rm -rf "$tmp"
