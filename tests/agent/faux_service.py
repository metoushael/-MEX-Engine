#!/usr/bin/env python3
"""Faux service de modèle de langage, pour essayer MEX Agent sans clé ni réseau.

Il imite les deux formats que MEX Agent sait parler :
  POST /v1/messages           API « Messages » d'Anthropic (Claude)
  POST /v1/chat/completions   format d'OpenAI (ChatGPT, OpenRouter, Mistral, Ollama...)
  GET  /v1/models             liste de modèles

Il vérifie la forme de chaque requête (en-têtes, alternance des messages, résultat fourni
pour chaque appel d'outil) et joue un scénario fixe : regarder le projet, écrire un script
fautif puis le corriger, créer une scène, régler une action, lancer le jeu, conclure.

    python3 tests/agent/faux_service.py 8765 journal.txt
    MEX_AGENT_PROVIDER=anthropic MEX_AGENT_URL=http://127.0.0.1:8765/v1 \
        ANTHROPIC_API_KEY=cle-de-test MEX_AGENT_AUTO=1 mex --editor --path <projet>

Ce n'est PAS une preuve que les vrais services acceptent ces requêtes : seulement que MEX
Agent produit ce que leur documentation décrit, et qu'il enchaîne correctement les outils.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
LOG = open(sys.argv[2], "a", encoding="utf-8") if len(sys.argv) > 2 else sys.stdout
KEY = "cle-de-test"

SCRIPT_FAUTIF = "extends Node2D\n\nfunc _ready():\n\tawait get_tree().create_timer(1.0).timeout\n\tprint(\"bonjour\")\n"
SCRIPT = "extends Node2D\n\nvar images = 0\n\nfunc _ready():\n\tprint(\"bonjour de l'agent\")\n\tprint(\"action tirer : \", InputMap.has_action(\"tirer\"))\n\nfunc _process(delta):\n\timages += 1\n"
SCENE = ("[mex_scene format=1]\n\n[ext_resource type=\"MexScript\" path=\"res://agent_test.mx\" id=\"1\"]\n"
         "[ext_resource type=\"Texture2D\" path=\"res://images/carre.png\" id=\"2\"]\n\n"
         "[node name=\"Essai\" type=\"Node2D\"]\nscript = ExtResource(\"1\")\n\n"
         "[node name=\"Carre\" type=\"Sprite2D\" parent=\".\"]\nposition = Vector2(200, 150)\ntexture = ExtResource(\"2\")\n")


def log(text):
    LOG.write(text + "\n")
    LOG.flush()


def scenario(step, results):
    """Renvoie (texte, [(outil, arguments)]) pour l'étape donnée ; results : résultats reçus."""
    if step == 0:
        return "Je commence par regarder le projet.", [
            ("lister_fichiers", {}),
            ("charger_competence", {"nom": "mexscript"}),
            ("lire_fichier", {"chemin": "../../etc/passwd"}),
            ("documentation_classe", {"classe": "TouchScreenButton"}),
        ]
    if step == 1:
        return "", [("ecrire_fichier", {"chemin": "res://agent_test.mx", "contenu": SCRIPT_FAUTIF})]
    if step == 2:
        return "Le script était invalide, je le corrige et je crée la scène.", [
            ("creer_image", {"chemin": "res://images/carre.png", "largeur": 48, "hauteur": 48, "couleur": "#3fa7ff"}),
            ("ecrire_fichier", {"chemin": "res://agent_test.mx", "contenu": SCRIPT}),
            ("ecrire_fichier", {"chemin": "res://agent_test.scn", "contenu": SCENE}),
            ("definir_parametre", {"nom": "input/tirer", "valeur": "[\"X\", \"JoyX\"]"}),
            ("remplacer_dans_fichier", {"chemin": "res://agent_test.mx", "ancien": "var images = 0", "nouveau": "var images = 0  # compteur"}),
        ]
    if step == 3:
        return "", [("lancer_jeu", {"scene": "res://agent_test.scn", "secondes": 1})]
    text = "## Bilan\nVoici ce que j'ai **observé** en lisant le projet et la sortie du jeu :\n"
    checks = [
        ("liste des fichiers reçue", "project.mex" in results.get("lister_fichiers", "")),
        ("compétence chargée", "MexScript" in results.get("charger_competence", "")),
        ("chemin hors projet refusé", "refus" in results.get("lire_fichier", "").lower()),
        ("classe documentée", "passby_press" in results.get("documentation_classe", "")),
        ("script fautif signalé", "INVALIDE" in results.get("ecrire_fichier#0", "")),
        ("script corrigé accepté", "correcte" in results.get("ecrire_fichier#1", "")),
        ("scène chargée", "se charge" in results.get("ecrire_fichier#2", "")),
        ("image créée", "Image créée" in results.get("creer_image", "")),
        ("paramètre enregistré", "input/tirer" in results.get("definir_parametre", "")),
        ("remplacement fait", "remplacé" in results.get("remplacer_dans_fichier", "")),
        ("le jeu a écrit son message", "bonjour de l'agent" in results.get("lancer_jeu", "")),
        ("l'action existe dans le jeu", "action tirer : true" in results.get("lancer_jeu", "")),
        ("aucune erreur dans la sortie", "0 erreur(s)" in results.get("lancer_jeu", "")),
    ]
    for name, ok in checks:
        text += ("- OK : " if ok else "- ECHEC : ") + name + "\n"
        log(("CONTROLE OK    " if ok else "CONTROLE ECHEC ") + name)
    text += "\nLe script final, dans `res://agent_test.mx` :\n```mexscript\n" + SCRIPT + "```\n1. Ouvrez la scène `agent_test.scn`.\n2. Lancez-la avec **F6**.\n\n> À vous d'essayer le jeu."
    log("SCENARIO TERMINE")
    return text, []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, code, payload):
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.endswith("/models"):
            log("GET " + self.path)
            self.reply(200, {"data": [{"id": "modele-b"}, {"id": "modele-a"}]})
        else:
            self.reply(404, {"error": {"message": "adresse inconnue"}})

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("content-length", "0"))).decode("utf-8"))
        problems = []
        results = {}
        counts = {}

        def remember(name, text):
            if name == "ecrire_fichier":
                index = counts.get(name, 0)
                counts[name] = index + 1
                results["%s#%d" % (name, index)] = text
            results[name] = text

        if self.path.endswith("/messages"):
            if self.headers.get("x-api-key") != KEY:
                return self.reply(401, {"type": "error", "error": {"type": "authentication_error", "message": "invalid x-api-key"}})
            if not self.headers.get("anthropic-version"):
                problems.append("en-tête anthropic-version absent")
            for field in ("model", "max_tokens", "messages", "system", "tools"):
                if field not in body:
                    problems.append("champ absent : " + field)
            for tool in body.get("tools", []):
                if not {"name", "description", "input_schema"} <= set(tool) or tool["input_schema"].get("type") != "object":
                    problems.append("outil mal décrit : " + str(tool.get("name")))
            messages = body.get("messages", [])
            if not messages or messages[0]["role"] != "user":
                problems.append("le premier message n'est pas de l'utilisateur")
            names = {}
            step = 0
            for i, message in enumerate(messages):
                if i and message["role"] == messages[i - 1]["role"]:
                    problems.append("deux messages '%s' de suite" % message["role"])
                if message["role"] == "assistant":
                    step += 1
                    uses = [b for b in message["content"] if b["type"] == "tool_use"]
                    if any(b["type"] == "text" and not b["text"] for b in message["content"]):
                        problems.append("bloc de texte vide")
                    for use in uses:
                        names[use["id"]] = use["name"]
                        if not isinstance(use["input"], dict):
                            problems.append("input n'est pas un objet")
                    if uses:
                        following = messages[i + 1] if i + 1 < len(messages) else {"role": "", "content": []}
                        got = [b.get("tool_use_id") for b in following["content"] if isinstance(b, dict) and b.get("type") == "tool_result"] if following["role"] == "user" else []
                        if got != [u["id"] for u in uses]:
                            problems.append("résultats d'outils absents ou dans le désordre")
                        for block in following["content"] if following["role"] == "user" else []:
                            remember(names.get(block.get("tool_use_id"), "?"), block.get("content", ""))
            text, calls = scenario(step, results)
            content = ([{"type": "text", "text": text}] if text else []) + [
                {"type": "tool_use", "id": "toolu_%d_%d" % (step, n), "name": name, "input": arguments} for n, (name, arguments) in enumerate(calls)]
            payload = {"id": "msg_test", "type": "message", "role": "assistant", "model": body.get("model"), "content": content,
                       "stop_reason": "tool_use" if calls else "end_turn", "usage": {"input_tokens": 1, "output_tokens": 1}}
        elif self.path.endswith("/chat/completions"):
            if self.headers.get("authorization") != "Bearer " + KEY:
                return self.reply(401, {"error": {"message": "Incorrect API key provided", "type": "invalid_request_error"}})
            messages = body.get("messages", [])
            if not messages or messages[0]["role"] != "system":
                problems.append("pas de message système en tête")
            for tool in body.get("tools", []):
                if tool.get("type") != "function" or not {"name", "description", "parameters"} <= set(tool.get("function", {})):
                    problems.append("outil mal décrit")
            if not body.get("tools"):
                problems.append("aucun outil")
            names = {}
            pending = []
            step = 0
            for message in messages[1:]:
                role = message["role"]
                if role == "tool":
                    if not pending or message.get("tool_call_id") != pending[0]:
                        problems.append("message 'tool' inattendu ou dans le désordre")
                    else:
                        pending.pop(0)
                    remember(names.get(message.get("tool_call_id"), "?"), message.get("content", ""))
                    continue
                if pending:
                    problems.append("appel d'outil sans résultat")
                    pending = []
                if role == "assistant":
                    step += 1
                    for call in message.get("tool_calls", []):
                        names[call["id"]] = call["function"]["name"]
                        pending.append(call["id"])
                        if call.get("type") != "function" or not isinstance(json.loads(call["function"]["arguments"]), dict):
                            problems.append("appel d'outil mal formé")
            if pending:
                problems.append("appel d'outil sans résultat en fin de liste")
            text, calls = scenario(step, results)
            message = {"role": "assistant", "content": text or None}
            if calls:
                message["tool_calls"] = [{"id": "call_%d_%d" % (step, n), "type": "function", "function": {"name": name, "arguments": json.dumps(arguments, ensure_ascii=False)}} for n, (name, arguments) in enumerate(calls)]
            payload = {"id": "chatcmpl-test", "object": "chat.completion", "model": body.get("model"),
                       "choices": [{"index": 0, "message": message, "finish_reason": "tool_calls" if calls else "stop"}]}
        else:
            return self.reply(404, {"error": {"message": "adresse inconnue : " + self.path}})
        log("POST %s  modèle=%s  messages=%d  %s" % (self.path, body.get("model"), len(body.get("messages", [])), "REQUETE CONFORME" if not problems else "REQUETE NON CONFORME : " + " ; ".join(problems)))
        self.reply(200, payload)


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
