#!/usr/bin/env python3
"""Faux service de modèle de langage, pour essayer MEX Agent sans clé ni réseau.

Il imite les deux formats que MEX Agent sait parler, sous /v1 comme les vrais services :
  POST /v1/messages           API « Messages » d'Anthropic (Claude)
  POST /v1/chat/completions   format d'OpenAI (ChatGPT, OpenRouter, Mistral, Ollama, LM Studio...)
  GET  /v1/models             liste de modèles
Tout autre chemin (/models, /chat/completions, /v1/v1/...) reçoit la réponse de LM Studio :
HTTP 200 et {"error": "Unexpected endpoint or method. (GET /models)"}.

Il vérifie la forme de chaque requête (en-têtes, alternance des messages, résultat fourni
pour chaque appel d'outil) et joue un scénario fixe : regarder le projet, écrire un script
fautif puis le corriger, créer une scène, régler une action, lancer le jeu, conclure.

    python3 tests/agent/faux_service.py 8765 journal.txt [genre]
    MEX_AGENT_PROVIDER=anthropic MEX_AGENT_URL=http://127.0.0.1:8765/v1 \
        ANTHROPIC_API_KEY=cle-de-test MEX_AGENT_AUTO=1 mex --editor --path <projet>

Le genre choisit le serveur local imité pour la question « ce modèle sait-il appeler des
outils ? » :
  lmstudio          GET /api/v1/models et GET /api/v0/models (LM Studio 0.4)
  lmstudio-ancien   GET /api/v0/models seulement (LM Studio 0.3)
  ollama            POST /api/show
  inconnu           aucune de ces API (llama.cpp, vLLM...) : valeur par défaut

Le nom du modèle demandé choisit son comportement :
  faux-gemma    modèle sans outils, au gabarit de Gemma : refuse le champ « tools »
                (« does not support tools », comme Ollama) et toute suite de rôles autre que
                [system] user assistant user... (« Conversation roles must alternate »,
                comme LM Studio)
  faux-strict   gabarit plus strict encore : erreur Jinja si « tools » est envoyé, et
                « Only user and assistant roles are supported! » pour un message system
  faux-gemini   outils, à la manière de Gemini 3 : le premier appel de chaque étape porte une
                « signature de réflexion » (extra_content.google.thought_signature), exigée
                telle quelle dans les requêtes suivantes ; sinon HTTP 400, erreur de Google
  faux-gemini-relais   Gemini derrière un relais qui ne transmet pas les signatures : il
                n'en envoie aucune et exige la valeur de remplacement documentée par Google
  faux-panne    répond par une page HTML 502, puis par un texte qui n'est pas du JSON, puis
                par une réponse vide, puis normalement
  tout autre    modèle à outils : joue le scénario

Ce n'est PAS une preuve que les vrais services acceptent ces requêtes : seulement que MEX
Agent produit ce que leur documentation décrit, et qu'il enchaîne correctement les outils.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
LOG = open(sys.argv[2], "a", encoding="utf-8") if len(sys.argv) > 2 else sys.stdout
KEY = "cle-de-test"
KIND = sys.argv[3] if len(sys.argv) > 3 else "inconnu"

# Modèles sans appel d'outils ; les autres en ont.
NO_TOOLS = ("faux-gemma", "faux-strict", "faux-panne")
MODELS = ["faux-outils", "faux-gemma", "faux-strict", "faux-panne", "faux-gemini", "faux-gemini-relais"]
GEMINI = ("faux-gemini", "faux-gemini-relais")
PLACEHOLDER = "skip_thought_signature_validator"
# Mot pour mot la réponse de Google, enveloppée dans un tableau.
SIGNATURE_ERROR = [{"error": {"code": 400, "message": "Function call is missing a thought_signature in functionCall parts. This is required for tools to work correctly, and missing thought_signature may lead to degraded model performance. Additional data, function call `default_api:lister_fichiers` , position 2. Please refer to https://ai.google.dev/gemini-api/docs/thought-signatures for more details.", "status": "INVALID_ARGUMENT"}}]
JINJA = ("Error rendering prompt with jinja template: \"Conversation roles must alternate user/assistant/user/assistant/...\".\n\n"
         "This is usually an issue with the model's prompt template.")
failures = {"faux-panne": 0}

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


def template_problem(messages, system_allowed):
    """Ce que refuserait le gabarit Jinja d'un modèle sans outils ; None si tout va bien."""
    expected = "user"
    for i, message in enumerate(messages):
        role = message.get("role")
        if i == 0 and role == "system":
            if not system_allowed:
                return "Only user and assistant roles are supported!"
            continue
        if role not in ("user", "assistant"):
            return "Only user and assistant roles are supported!" if not system_allowed else JINJA
        if role != expected or "tool_calls" in message:
            return JINJA
        if not isinstance(message.get("content"), str) or not message["content"].strip():
            return JINJA
        expected = "assistant" if role == "user" else "user"
    return JINJA if expected == "user" else None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, code, payload, raw=None, kind="application/json"):
        data = raw.encode("utf-8") if raw is not None else json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("content-type", kind)
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def unexpected(self, method):
        # Mot pour mot ce que répond LM Studio à un chemin qu'il ne connaît pas.
        log("CHEMIN INATTENDU %s %s" % (method, self.path))
        if KIND.startswith("lmstudio"):
            return self.reply(200, {"error": "Unexpected endpoint or method. (%s %s)" % (method, self.path)})
        return self.reply(404, None, raw="404 page not found", kind="text/plain")

    def do_GET(self):
        if self.path == "/v1/models":
            log("GET " + self.path)
            return self.reply(200, {"object": "list", "data": [{"id": name, "object": "model"} for name in reversed(MODELS)]})
        if self.path == "/api/v1/models" and KIND == "lmstudio":
            log("GET " + self.path)
            return self.reply(200, {"models": [{"type": "llm", "key": name, "display_name": name, "format": "gguf", "max_context_length": 32768,
                                                "loaded_instances": [{"id": name, "config": {"context_length": 32768}}],
                                                "capabilities": {"vision": False, "trained_for_tool_use": name not in NO_TOOLS}} for name in MODELS]})
        if self.path == "/api/v0/models" and KIND.startswith("lmstudio"):
            log("GET " + self.path)
            data = []
            for name in MODELS:
                item = {"id": name, "object": "model", "type": "llm", "state": "loaded", "max_context_length": 32768, "loaded_context_length": 32768}
                if name not in NO_TOOLS:
                    item["capabilities"] = ["tool_use"]
                data.append(item)
            return self.reply(200, {"object": "list", "data": data})
        return self.unexpected("GET")

    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get("content-length", "0"))).decode("utf-8")
        try:
            body = json.loads(raw)
        except ValueError:
            log("POST %s  CORPS ILLISIBLE" % self.path)
            return self.reply(400, {"error": {"message": "corps de requête illisible"}})
        problems = []
        results = {}
        counts = {}

        def remember(name, text):
            if name == "ecrire_fichier":
                index = counts.get(name, 0)
                counts[name] = index + 1
                results["%s#%d" % (name, index)] = text
            results[name] = text

        if self.path == "/api/show" and KIND == "ollama":
            name = body.get("model", "")
            log("POST /api/show  modèle=%s" % name)
            if name not in MODELS:
                return self.reply(404, {"error": "model '%s' not found" % name})
            return self.reply(200, {"details": {"family": "essai"}, "capabilities": ["completion"] if name in NO_TOOLS else ["completion", "tools"]})
        if self.path == "/v1/messages":
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
                        if use["id"] in names:
                            problems.append("identifiant d'appel en double")
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
        elif self.path == "/v1/chat/completions":
            # Un serveur local n'attend pas de clé ; une clé fausse est refusée.
            if self.headers.get("authorization") not in (None, "Bearer " + KEY):
                return self.reply(401, {"error": {"message": "Incorrect API key provided", "type": "invalid_request_error"}})
            messages = body.get("messages", [])
            model = body.get("model", "")
            if body.get("stream") is not False:
                problems.append("« stream » n'est pas false")
            if model in NO_TOOLS:
                return self.chat_without_tools(body, model, messages)
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
            unsigned = 0
            previous = "system"
            for message in messages[1:]:
                role = message["role"]
                if role == "tool":
                    if not pending or message.get("tool_call_id") != pending[0]:
                        problems.append("message 'tool' inattendu ou dans le désordre")
                    else:
                        pending.pop(0)
                    if not isinstance(message.get("content"), str) or not message["content"].strip():
                        problems.append("résultat d'outil vide")
                    remember(names.get(message.get("tool_call_id"), "?"), message.get("content", ""))
                    previous = role
                    continue
                if pending:
                    problems.append("appel d'outil sans résultat")
                    pending = []
                if role == previous:
                    problems.append("deux messages '%s' de suite" % role)
                if role == "user" and previous == "tool":
                    problems.append("message 'user' juste après un message 'tool'")
                if role not in ("user", "assistant"):
                    problems.append("rôle inattendu : %s" % role)
                if role == "user" and (not isinstance(message.get("content"), str) or not message["content"].strip()):
                    problems.append("message 'user' vide")
                if role == "assistant":
                    if model in GEMINI and message.get("tool_calls"):
                        # La signature de l'étape doit revenir sur son premier appel, et lui seul.
                        expected = "sig-%d" % step if model == "faux-gemini" else PLACEHOLDER
                        if message["tool_calls"][0].get("extra_content", {}).get("google", {}).get("thought_signature") != expected:
                            unsigned += 1
                        if any("extra_content" in call for call in message["tool_calls"][1:]):
                            problems.append("signature sur un appel parallèle")
                    elif any("extra_content" in call for call in message.get("tool_calls", [])):
                        problems.append("extra_content envoyé à un modèle qui n'en a pas donné")
                    step += 1
                    if not message.get("tool_calls") and not (message.get("content") or "").strip():
                        problems.append("message 'assistant' vide")
                    for call in message.get("tool_calls", []):
                        if call["id"] in names:
                            problems.append("identifiant d'appel en double")
                        names[call["id"]] = call["function"]["name"]
                        pending.append(call["id"])
                        if call.get("type") != "function" or not isinstance(json.loads(call["function"]["arguments"]), dict):
                            problems.append("appel d'outil mal formé")
                previous = role
            if pending:
                problems.append("appel d'outil sans résultat en fin de liste")
            if unsigned:
                log("POST %s  modèle=%s  messages=%d  REFUS signature (%d étape(s) sans signature)" % (self.path, model, len(messages), unsigned))
                return self.reply(400, SIGNATURE_ERROR)
            if model in GEMINI and step:
                log("SIGNATURES CONFORMES (%d étape(s))" % step)
            text, calls = scenario(step, results)
            message = {"role": "assistant", "content": text or None}
            if calls:
                # Comme plusieurs serveurs locaux : toujours les mêmes identifiants, d'un tour à
                # l'autre. MEX Agent doit les rendre uniques dans la conversation.
                message["tool_calls"] = [{"id": "call_%d" % n, "type": "function", "function": {"name": name, "arguments": json.dumps(arguments, ensure_ascii=False)}} for n, (name, arguments) in enumerate(calls)]
                if model == "faux-gemini":
                    message["tool_calls"][0]["extra_content"] = {"google": {"thought_signature": "sig-%d" % step}}
            payload = {"id": "chatcmpl-test", "object": "chat.completion", "model": body.get("model"),
                       "choices": [{"index": 0, "message": message, "finish_reason": "tool_calls" if calls else "stop"}]}
        else:
            return self.unexpected("POST")
        log("POST %s  modèle=%s  messages=%d  %s" % (self.path, body.get("model"), len(body.get("messages", [])), "REQUETE CONFORME" if not problems else "REQUETE NON CONFORME : " + " ; ".join(problems)))
        self.reply(200, payload)

    def chat_without_tools(self, body, model, messages):
        """Modèle sans appel d'outils : mêmes refus qu'un vrai serveur local."""
        head = "POST %s  modèle=%s  messages=%d  " % (self.path, model, len(messages))
        if "tools" in body:
            if model == "faux-strict":
                log(head + "REFUS gabarit (outils envoyés)")
                return self.reply(400, {"error": JINJA})
            log(head + "REFUS outils")
            return self.reply(400, {"error": {"message": "registry.ollama.ai/library/%s does not support tools" % model, "type": "api_error", "param": None, "code": None}})
        problem = template_problem(messages, model != "faux-strict")
        if problem:
            log(head + "REFUS gabarit : " + problem.split("\n")[0])
            return self.reply(400, {"error": problem})
        if model == "faux-panne":
            failures[model] += 1
            if failures[model] == 1:
                log(head + "PANNE page HTML 502")
                return self.reply(502, None, raw="<html><head><title>502 Bad Gateway</title></head><body><h1>502 Bad Gateway</h1></body></html>", kind="text/html")
            if failures[model] == 2:
                log(head + "PANNE texte qui n'est pas du JSON")
                return self.reply(200, None, raw="data: [DONE]", kind="text/event-stream")
            if failures[model] == 3:
                log(head + "PANNE réponse vide")
                return self.reply(200, {"choices": [{"index": 0, "message": {"role": "assistant", "content": ""}, "finish_reason": "stop"}]})
        turns = sum(1 for message in messages if message["role"] == "user")
        log(head + "CONVERSATION CONFORME (rôles : %s)" % " ".join(message["role"] for message in messages))
        text = "<think>\nRéflexion du faux modèle.\n</think>\n\nRéponse %d du faux modèle sans outils." % turns
        self.reply(200, {"id": "chatcmpl-test", "object": "chat.completion", "model": model,
                         "choices": [{"index": 0, "message": {"role": "assistant", "content": text}, "finish_reason": "stop"}]})


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
