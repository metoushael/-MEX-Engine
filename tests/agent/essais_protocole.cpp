// Essais du protocole de MEX Agent (editor/editor_agent_protocol.*), sans réseau ni éditeur :
// adresses, requêtes, historiques, appels d'outils, réponses JSON, erreurs HTTP, modèles
// locaux sans outils. Programme « mex-essais-agent » ; code de sortie 0 si tout passe.
//
//     build/rel/bin/mex-essais-agent
//
// Les réponses d'erreur et de capacités utilisées ici sont celles que renvoient réellement
// LM Studio 0.4 et Ollama 0.40 (relevées sur des serveurs en marche).
#include "editor/editor_agent_protocol.h"

#include "core/io/json.h"
#include "core/string/ustring.h"

#include <cstdio>

using namespace EditorAgentProtocol;
typedef EditorAgent::Message Message;
typedef EditorAgent::ToolCall ToolCall;

namespace {

int passed = 0;
int failed = 0;

void check(bool p_ok, const String &p_name, const String &p_detail = String()) {
	if (p_ok) {
		passed++;
		return;
	}
	failed++;
	printf("ECHEC : %s%s%s\n", p_name.c_str(), p_detail.empty() ? "" : "\n        ", p_detail.c_str());
}

void check_eq(const String &p_got, const String &p_expected, const String &p_name) {
	check(p_got == p_expected, p_name, "obtenu « " + p_got + " », attendu « " + p_expected + " »");
}

void section(const char *p_title) {
	printf("--- %s\n", p_title);
}

String S(const Variant &p_value) {
	return p_value.get_type() == Variant::NIL ? String("<null>") : p_value.as_string();
}

Variant at(const Dictionary &p_dictionary, const char *p_key) {
	return p_dictionary.get(Variant(p_key), Variant());
}

Message user(const String &p_text) {
	Message message;
	message.kind = Message::USER;
	message.text = p_text;
	return message;
}

Message assistant(const String &p_text) {
	Message message;
	message.kind = Message::ASSISTANT;
	message.text = p_text;
	return message;
}

Message notice(const String &p_text) {
	Message message;
	message.kind = Message::NOTICE;
	message.text = p_text;
	return message;
}

ToolCall call(const String &p_id, const String &p_name, const String &p_result, bool p_error = false) {
	ToolCall item;
	item.id = p_id;
	item.name = p_name;
	item.arguments[Variant("chemin")] = Variant("res://joueur.mx");
	item.result = p_result;
	item.is_error = p_error;
	item.state = ToolCall::DONE;
	return item;
}

Message assistant_calls(const String &p_text, const std::vector<ToolCall> &p_calls) {
	Message message = assistant(p_text);
	message.calls = p_calls;
	return message;
}

Array definitions() {
	Array list;
	for (const char *name : { "lister_fichiers", "lire_fichier", "ecrire_fichier" }) {
		Dictionary schema;
		schema[Variant("type")] = Variant("object");
		schema[Variant("properties")] = Variant(Dictionary());
		Dictionary tool;
		tool[Variant("name")] = Variant(name);
		tool[Variant("description")] = Variant("Outil d'essai.");
		tool[Variant("parameters")] = Variant(schema);
		list.push_back(Variant(tool));
	}
	return list;
}

const char *SYSTEM = "Tu es MEX Agent.";

Dictionary openai(const std::vector<Message> &p_messages, bool p_tools = true, bool p_system_role = true, bool p_local = true) {
	Dialect dialect;
	dialect.tools = p_tools;
	dialect.system_role = p_system_role;
	dialect.local = p_local;
	return build_request(false, "modele", SYSTEM, p_messages, definitions(), 4096, dialect);
}

Dictionary anthropic(const std::vector<Message> &p_messages) {
	return build_request(true, "claude", SYSTEM, p_messages, definitions(), 4096, Dialect());
}

String roles(const Dictionary &p_request) {
	String text;
	const Array list = at(p_request, "messages").as_array();
	for (int i = 0; i < list.size(); i++) {
		text += (i ? " " : "") + S(at(list[i].as_dictionary(), "role"));
	}
	return text;
}

// Ce qu'exige le format « chat completions » : rôles connus, aucun contenu vide, un message
// « tool » par appel, dans l'ordre et avec le même identifiant, identifiants uniques, jamais
// deux « user » ni deux « assistant » de suite. Renvoie le premier défaut, ou un texte vide.
String openai_problem(const Dictionary &p_request) {
	const Array list = at(p_request, "messages").as_array();
	if (list.size() == 0) {
		return "aucun message";
	}
	std::vector<String> pending;
	std::set<String> ids;
	String previous;
	for (int i = 0; i < list.size(); i++) {
		const Dictionary message = list[i].as_dictionary();
		const String role = S(at(message, "role"));
		const Variant content = at(message, "content");
		const String where = "message " + itos(i) + " (" + role + ")";
		if (role == "system") {
			if (i != 0) {
				return where + " : « system » ailleurs qu'en tête";
			}
		} else if (role == "tool") {
			if (pending.empty() || S(at(message, "tool_call_id")) != pending.front()) {
				return where + " : résultat inattendu ou dans le désordre";
			}
			pending.erase(pending.begin());
		} else if (role == "user" || role == "assistant") {
			if (!pending.empty()) {
				return where + " : appel d'outil resté sans résultat";
			}
			if (role == "user" && previous == "tool") {
				return where + " : « user » juste après « tool » (refusé par Mistral et par plusieurs gabarits)";
			}
			if (role == previous) {
				return where + " : deux messages « " + role + " » de suite";
			}
		} else {
			return where + " : rôle inconnu";
		}
		const bool has_calls = message.has(Variant("tool_calls"));
		if (content.get_type() == Variant::NIL) {
			if (!(role == "assistant" && has_calls)) {
				return where + " : contenu null";
			}
		} else if (content.get_type() != Variant::STRING) {
			return where + " : le contenu n'est pas un texte";
		} else if (str_strip(content.as_string()).empty() && !(role == "assistant" && has_calls)) {
			return where + " : contenu vide";
		}
		if (has_calls) {
			if (role != "assistant") {
				return where + " : tool_calls hors d'un message « assistant »";
			}
			const Array calls = at(message, "tool_calls").as_array();
			if (calls.size() == 0) {
				return where + " : tool_calls vide";
			}
			for (int j = 0; j < calls.size(); j++) {
				const Dictionary item = calls[j].as_dictionary();
				const String id = S(at(item, "id"));
				const Dictionary function = at(item, "function").as_dictionary();
				Variant arguments;
				if (id.empty() || id == "<null>" || !ids.insert(id).second) {
					return where + " : identifiant d'appel vide ou en double (" + id + ")";
				}
				if (S(at(item, "type")) != "function" || S(at(function, "name")).empty()) {
					return where + " : appel mal formé";
				}
				if (at(function, "arguments").get_type() != Variant::STRING || !JSON::parse(at(function, "arguments").as_string(), arguments) || arguments.get_type() != Variant::DICTIONARY) {
					return where + " : les arguments ne sont pas le texte JSON d'un objet";
				}
				pending.push_back(id);
			}
		}
		previous = role;
	}
	if (!pending.empty()) {
		return "appel d'outil sans résultat en fin de conversation";
	}
	if (previous != "user" && previous != "tool") {
		return "la conversation ne se termine pas par l'utilisateur ou un résultat d'outil";
	}
	return String();
}

// Le gabarit Jinja de Gemma (et de bien d'autres modèles sans outils) : un « system » facultatif
// en tête, puis une alternance stricte user / assistant qui commence par « user » ; tout autre
// rôle déclenche « Conversation roles must alternate user/assistant/user/assistant/... ».
String gemma_problem(const Dictionary &p_request, bool p_system_allowed) {
	if (p_request.has(Variant("tools"))) {
		return "champ « tools » envoyé";
	}
	const Array list = at(p_request, "messages").as_array();
	int index = 0;
	for (int i = 0; i < list.size(); i++) {
		const Dictionary message = list[i].as_dictionary();
		const String role = S(at(message, "role"));
		if (i == 0 && role == "system" && p_system_allowed) {
			continue;
		}
		if (role != (index % 2 == 0 ? "user" : "assistant")) {
			return "Conversation roles must alternate (message " + itos(i) + " : " + role + ")";
		}
		if (message.has(Variant("tool_calls")) || message.has(Variant("tool_call_id"))) {
			return "champ d'outil dans le message " + itos(i);
		}
		if (at(message, "content").get_type() != Variant::STRING || str_strip(at(message, "content").as_string()).empty()) {
			return "contenu vide dans le message " + itos(i);
		}
		index++;
	}
	return index % 2 == 1 ? String() : String("la conversation ne se termine pas par l'utilisateur");
}

String anthropic_problem(const Dictionary &p_request) {
	const Array list = at(p_request, "messages").as_array();
	std::vector<String> pending;
	String previous;
	for (int i = 0; i < list.size(); i++) {
		const Dictionary message = list[i].as_dictionary();
		const String role = S(at(message, "role"));
		const String where = "message " + itos(i) + " (" + role + ")";
		if (i == 0 && role != "user") {
			return "le premier message n'est pas de l'utilisateur";
		}
		if (role == previous) {
			return where + " : deux messages de même rôle de suite";
		}
		previous = role;
		const Variant content = at(message, "content");
		if (content.get_type() == Variant::STRING) {
			if (!pending.empty()) {
				return where + " : résultats d'outils absents";
			}
			if (str_strip(content.as_string()).empty()) {
				return where + " : texte vide";
			}
			continue;
		}
		const Array blocks = content.as_array();
		if (blocks.size() == 0) {
			return where + " : aucun bloc";
		}
		std::vector<String> results;
		std::vector<String> uses;
		for (int j = 0; j < blocks.size(); j++) {
			const Dictionary block = blocks[j].as_dictionary();
			const String type = S(at(block, "type"));
			if (type == "text" && str_strip(S(at(block, "text"))).empty()) {
				return where + " : bloc de texte vide";
			} else if (type == "tool_use") {
				uses.push_back(S(at(block, "id")));
				if (at(block, "input").get_type() != Variant::DICTIONARY) {
					return where + " : input n'est pas un objet";
				}
			} else if (type == "tool_result") {
				results.push_back(S(at(block, "tool_use_id")));
				if (str_strip(S(at(block, "content"))).empty()) {
					return where + " : résultat vide";
				}
			}
		}
		if (role == "user" && results != pending) {
			return where + " : résultats d'outils absents ou dans le désordre";
		}
		if (role == "assistant" && !pending.empty()) {
			return where + " : résultats d'outils absents";
		}
		pending = uses;
	}
	return pending.empty() ? String() : String("appel d'outil sans résultat en fin de conversation");
}

bool parse(const String &p_body, int p_http, Message &r_message, String &r_error, String &r_stop, bool p_anthropic = false) {
	std::set<String> used;
	return parse_response(p_anthropic, p_body, p_http, used, r_message, r_error, r_stop);
}

// ---------------------------------------------------------------------------

void test_urls() {
	section("Adresses");
	const char *cases[][2] = {
		// Serveurs locaux : « /v1 » jamais oublié, jamais doublé.
		{ "http://localhost:1234", "http://localhost:1234/v1" },
		{ "http://localhost:1234/", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1/", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1/v1", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1/v1/", "http://localhost:1234/v1" },
		{ "http://localhost:1234//v1//", "http://localhost:1234/v1" },
		{ "  http://localhost:1234/v1  ", "http://localhost:1234/v1" },
		{ "\"http://localhost:1234/v1\"", "http://localhost:1234/v1" },
		{ "localhost:1234", "http://localhost:1234/v1" },
		{ "127.0.0.1:1234", "http://127.0.0.1:1234/v1" },
		{ "http://127.0.0.1:1234", "http://127.0.0.1:1234/v1" },
		{ "http://0.0.0.0:1234", "http://127.0.0.1:1234/v1" },
		{ "http://[::1]:1234", "http://[::1]:1234/v1" },
		{ "http://192.168.1.20:1234", "http://192.168.1.20:1234/v1" },
		{ "http://monpc:1234", "http://monpc:1234/v1" },
		{ "HTTP://LocalHost:1234/V1", "http://LocalHost:1234/V1" },
		// Point d'accès collé en entier : on garde la base.
		{ "http://localhost:1234/v1/chat/completions", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1/chat/completions/", "http://localhost:1234/v1" },
		{ "http://localhost:1234/chat/completions", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1/models", "http://localhost:1234/v1" },
		{ "http://localhost:1234/models", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1/completions", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1/responses", "http://localhost:1234/v1" },
		{ "http://localhost:1234/v1/models?x=1", "http://localhost:1234/v1" },
		// LM Studio : son API propre n'est pas celle que parle MEX Agent.
		{ "http://localhost:1234/api/v0", "http://localhost:1234/api/v0" },
		// Ollama.
		{ "http://localhost:11434", "http://localhost:11434/v1" },
		{ "http://localhost:11434/v1", "http://localhost:11434/v1" },
		{ "http://localhost:11434/api", "http://localhost:11434/v1" },
		{ "http://localhost:11434/api/chat", "http://localhost:11434/v1" },
		{ "http://localhost:11434/api/tags", "http://localhost:11434/v1" },
		{ "http://localhost:11434/api/generate", "http://localhost:11434/v1" },
		{ "http://localhost:11434/v1/chat/completions", "http://localhost:11434/v1" },
		{ "localhost:11434", "http://localhost:11434/v1" },
		// Services distants : chemin conservé, point d'accès retiré.
		{ "https://api.openai.com/v1", "https://api.openai.com/v1" },
		{ "https://api.openai.com/v1/", "https://api.openai.com/v1" },
		{ "https://api.openai.com/v1/chat/completions", "https://api.openai.com/v1" },
		{ "https://api.anthropic.com/v1", "https://api.anthropic.com/v1" },
		{ "https://api.anthropic.com/v1/messages", "https://api.anthropic.com/v1" },
		{ "https://openrouter.ai/api/v1", "https://openrouter.ai/api/v1" },
		{ "https://openrouter.ai/api/v1/models", "https://openrouter.ai/api/v1" },
		{ "https://generativelanguage.googleapis.com/v1beta/openai", "https://generativelanguage.googleapis.com/v1beta/openai" },
		{ "https://generativelanguage.googleapis.com/v1beta/openai/", "https://generativelanguage.googleapis.com/v1beta/openai" },
		{ "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions", "https://generativelanguage.googleapis.com/v1beta/openai" },
		{ "https://api.mistral.ai/v1", "https://api.mistral.ai/v1" },
		{ "https://api.deepseek.com/v1", "https://api.deepseek.com/v1" },
		{ "https://api.deepseek.com", "https://api.deepseek.com" },
		{ "https://api.groq.com/openai/v1", "https://api.groq.com/openai/v1" },
		{ "https://api.groq.com/openai/v1/chat/completions", "https://api.groq.com/openai/v1" },
		{ "https://models.github.ai/inference", "https://models.github.ai/inference" },
		{ "https://api.z.ai/api/paas/v4", "https://api.z.ai/api/paas/v4" },
		{ "api.mistral.ai/v1", "https://api.mistral.ai/v1" },
		// Rien d'exploitable.
		{ "", "" },
		{ "   ", "" },
		{ "pas une adresse", "" },
	};
	for (const auto &item : cases) {
		check_eq(normalize_url(item[0]), item[1], String("adresse « ") + item[0] + " »");
	}
	// Normaliser deux fois ne change plus rien.
	for (const auto &item : cases) {
		const String once = normalize_url(item[0]);
		check_eq(normalize_url(once), once, String("adresse stable « ") + item[0] + " »");
	}
	check_eq(normalize_url("http://localhost:1234") + "/models", "http://localhost:1234/v1/models", "route GET /v1/models");
	check_eq(normalize_url("http://localhost:1234/v1/") + "/chat/completions", "http://localhost:1234/v1/chat/completions", "route POST /v1/chat/completions");
	check_eq(normalize_url("http://localhost:11434/api/chat") + "/chat/completions", "http://localhost:11434/v1/chat/completions", "route Ollama");

	check_eq(server_root("http://localhost:1234/v1"), "http://localhost:1234", "racine LM Studio");
	check_eq(server_root("http://127.0.0.1:11434/v1"), "http://127.0.0.1:11434", "racine Ollama");
	check_eq(server_root("https://openrouter.ai/api/v1"), "https://openrouter.ai", "racine distante");

	for (const char *url : { "http://localhost:1234/v1", "http://127.0.0.1:11434/v1", "http://[::1]:8080/v1", "http://192.168.0.12:1234/v1", "http://10.0.0.5:1234", "http://172.20.1.1:1234", "http://monpc:1234/v1", "http://serveur.local:1234", "http://host.docker.internal:11434/v1", "http://100.100.20.3:1234" }) {
		check(is_local_url(url), String("serveur local : ") + url);
	}
	for (const char *url : { "https://api.openai.com/v1", "https://openrouter.ai/api/v1", "https://api.anthropic.com/v1", "http://172.40.1.1:1234", "https://8.8.8.8/v1", "https://100.20.1.1/v1", "" }) {
		check(!is_local_url(url), String("service distant : ") + url);
	}
}

void test_requests_openai() {
	section("Requêtes « chat completions » avec outils");
	{
		const Dictionary request = openai({ user("Bonjour") });
		check_eq(S(at(request, "model")), "modele", "champ model");
		check(at(request, "stream").get_type() == Variant::BOOL && !at(request, "stream").as_bool(), "stream: false");
		check(!request.has(Variant("max_tokens")), "pas de max_tokens (refusé par les modèles récents d'OpenAI)");
		check_eq(roles(request), "system user", "une demande : system user");
		check_eq(openai_problem(request), "", "une demande : forme valide");
		const Array tools = at(request, "tools").as_array();
		check(tools.size() == 3, "trois outils décrits");
		const Dictionary tool = tools[0].as_dictionary();
		check_eq(S(at(tool, "type")), "function", "outil de type function");
		const Dictionary function = at(tool, "function").as_dictionary();
		check(function.has(Variant("name")) && function.has(Variant("description")) && function.has(Variant("parameters")), "outil : name, description, parameters");
		check_eq(S(at(at(request, "messages").as_array()[0].as_dictionary(), "content")), SYSTEM, "instructions dans le message system");
	}
	{
		// Demande restée sans réponse (erreur de connexion), puis nouvelle demande : c'est le cas
		// qui donnait « Conversation roles must alternate » après une adresse erronée.
		const Dictionary request = openai({ user("Première demande"), notice("Le service a refusé la requête"), user("Deuxième demande") });
		check_eq(roles(request), "system user", "deux demandes de suite : un seul message user");
		check_eq(openai_problem(request), "", "deux demandes de suite : forme valide");
		const String content = S(at(at(request, "messages").as_array()[1].as_dictionary(), "content"));
		check(content.find("Première demande") != String::npos && content.find("Deuxième demande") != String::npos, "deux demandes de suite : les deux textes sont gardés");
		check(content.find("refusé") == String::npos, "les messages de l'éditeur ne partent pas au modèle");
	}
	{
		const Dictionary request = openai({ user("a"), notice("x"), user("b"), notice("y"), user("c") });
		check_eq(roles(request), "system user", "trois demandes de suite : un seul message user");
	}
	{
		// Réponse vide du modèle, messages faits d'espaces.
		const Dictionary request = openai({ user("Un"), assistant(""), user("Deux"), assistant("  \n "), user("   "), user("Trois") });
		check_eq(roles(request), "system user", "messages vides retirés");
		check_eq(openai_problem(request), "", "messages vides : forme valide");
	}
	{
		const Dictionary request = openai({ assistant("Bonjour, je suis MEX Agent."), user("Salut") });
		check_eq(roles(request), "system user", "la conversation commence par l'utilisateur");
	}
	{
		const Dictionary request = openai({ user("Un"), assistant("Réponse"), assistant("Suite"), user("Deux") });
		check_eq(roles(request), "system user assistant user", "deux réponses de suite : un seul message assistant");
		check_eq(openai_problem(request), "", "deux réponses de suite : forme valide");
	}
	{
		// Tour complet avec outils.
		const std::vector<Message> history = {
			user("Lis le joueur"),
			assistant_calls("Je regarde.", { call("call_1", "lister_fichiers", "project.mex\njoueur.mx"), call("call_2", "lire_fichier", "extends Node2D") }),
			assistant_calls("", { call("call_3", "ecrire_fichier", "Chemin refusé", true) }),
			assistant("C'est fait."),
			user("Merci, continue"),
		};
		const Dictionary request = openai(history);
		check_eq(roles(request), "system user assistant tool tool assistant tool assistant user", "tour avec outils : ordre des rôles");
		check_eq(openai_problem(request), "", "tour avec outils : forme valide");
		const Array list = at(request, "messages").as_array();
		const Dictionary first = list[2].as_dictionary();
		check_eq(S(at(first, "content")), "Je regarde.", "texte qui accompagne des appels");
		const Dictionary item = at(first, "tool_calls").as_array()[0].as_dictionary();
		check_eq(S(at(item, "id")), "call_1", "identifiant du modèle conservé");
		check_eq(S(at(at(item, "function").as_dictionary(), "arguments")), "{\"chemin\":\"res://joueur.mx\"}", "arguments en texte JSON");
		check_eq(S(at(list[3].as_dictionary(), "tool_call_id")), "call_1", "premier résultat lié au premier appel");
		check_eq(S(at(list[4].as_dictionary(), "tool_call_id")), "call_2", "second résultat lié au second appel");
		check_eq(S(at(list[4].as_dictionary(), "content")), "extends Node2D", "contenu du résultat");
		check_eq(S(at(list[6].as_dictionary(), "content")), "ERREUR : Chemin refusé", "résultat en erreur signalé au modèle");
		// Appels sans texte : "" pour un serveur local, null pour un service distant.
		check(at(list[5].as_dictionary(), "content").get_type() == Variant::STRING && S(at(list[5].as_dictionary(), "content")).empty(), "appels sans texte, serveur local : contenu \"\"");
		const Dictionary remote = openai(history, true, true, false);
		check(at(at(remote, "messages").as_array()[5].as_dictionary(), "content").get_type() == Variant::NIL, "appels sans texte, service distant : contenu null");
		check_eq(openai_problem(remote), "", "tour avec outils, service distant : forme valide");
	}
	{
		// Demande en cours : la conversation se termine par les résultats d'outils.
		const Dictionary request = openai({ user("Lis"), assistant_calls("", { call("a", "lire_fichier", "contenu") }) });
		check_eq(roles(request), "system user assistant tool", "résultats en fin de conversation");
		check_eq(openai_problem(request), "", "résultats en fin de conversation : forme valide");
	}
	{
		// Identifiants absents ou en double (certains modèles locaux renvoient toujours le même).
		const Dictionary request = openai({ user("Lis"), assistant_calls("", { call("", "lister_fichiers", "a"), call("", "lire_fichier", "b") }), assistant_calls("", { call("call_0", "lire_fichier", "c") }), assistant_calls("", { call("call_0", "lire_fichier", "d") }) });
		check_eq(openai_problem(request), "", "identifiants absents ou en double : rendus uniques, résultats appariés");
	}
	{
		// Résultat vide : un contenu vide est refusé par plusieurs services.
		const Dictionary request = openai({ user("Lis"), assistant_calls("", { call("a", "lire_fichier", ""), call("b", "lire_fichier", "  ", true) }) });
		check_eq(openai_problem(request), "", "résultat d'outil vide : contenu de remplacement");
	}
	{
		// Texte du modèle, puis réponse avec appels : un seul message assistant.
		const Dictionary request = openai({ user("Lis"), assistant("Je réfléchis."), assistant_calls("Je lis.", { call("a", "lire_fichier", "x") }), user("Suite") });
		check_eq(roles(request), "system user assistant tool assistant user", "texte puis appels : un seul message assistant, puis une réponse avant la demande");
		check_eq(openai_problem(request), "", "texte puis appels : forme valide");
	}
	{
		// Interruption : demande, appels, puis deux demandes de suite.
		const Dictionary request = openai({ user("Un"), assistant_calls("", { call("a", "lire_fichier", "Interrompu par l'utilisateur avant l'exécution.", true) }), notice("Interrompu."), user("Deux"), notice("Connexion impossible"), user("Trois") });
		check_eq(roles(request), "system user assistant tool assistant user", "interruption puis deux demandes : jamais « user » après « tool »");
		check_eq(openai_problem(request), "", "interruption puis deux demandes : forme valide");
	}
	{
		// Sans rôle « system » : les instructions ouvrent la première demande.
		const Dictionary request = openai({ user("Bonjour"), assistant("Salut"), user("Suite") }, true, false);
		check_eq(roles(request), "user assistant user", "sans rôle system : aucun message system");
		const String content = S(at(at(request, "messages").as_array()[0].as_dictionary(), "content"));
		check(content.find(SYSTEM) == 0 && content.find("Bonjour") != String::npos, "sans rôle system : instructions en tête de la première demande");
		check_eq(openai_problem(request), "", "sans rôle system : forme valide");
	}
	{
		const Dictionary request = build_request(false, "modele", "", { user("Bonjour") }, definitions(), 4096, Dialect());
		check_eq(roles(request), "user", "instructions vides : pas de message system vide");
	}
}

void test_requests_without_tools() {
	section("Requêtes pour un modèle sans outils");
	{
		const Dictionary request = openai({ user("Bonjour") }, false);
		check(!request.has(Variant("tools")), "aucun champ tools");
		check(!request.has(Variant("tool_choice")), "aucun champ tool_choice");
		check_eq(roles(request), "system user", "une demande : system user");
		check_eq(gemma_problem(request, true), "", "une demande : acceptée par le gabarit de Gemma");
	}
	{
		// La conversation de l'utilisateur : deux demandes ratées (adresse erronée), puis la bonne.
		const Dictionary request = openai({ user("salut"), notice("Unexpected endpoint or method. (POST /chat/completions)"), user("salut"), notice("idem"), user("tu es là ?") }, false);
		check_eq(roles(request), "system user", "demandes ratées puis nouvelle demande");
		check_eq(gemma_problem(request, true), "", "demandes ratées : acceptées par le gabarit de Gemma");
	}
	{
		// Historique avec outils (modèle changé en cours de conversation) : tout devient du texte.
		const std::vector<Message> history = {
			user("Lis le joueur"),
			assistant_calls("Je regarde.", { call("call_1", "lister_fichiers", "project.mex"), call("call_2", "lire_fichier", "extends Node2D") }),
			assistant_calls("", { call("call_3", "ecrire_fichier", "Chemin refusé", true) }),
			assistant("C'est fait."),
			user("Merci, continue"),
		};
		const Dictionary request = openai(history, false);
		check_eq(roles(request), "system user assistant user assistant user assistant user", "anciens appels d'outils : alternance stricte");
		check_eq(gemma_problem(request, true), "", "anciens appels d'outils : acceptés par le gabarit de Gemma");
		const String text = JSON::stringify(Variant(request));
		check(text.find("\"tool\"") == String::npos && text.find("tool_calls") == String::npos && text.find("tool_call_id") == String::npos, "aucun rôle tool, aucun tool_calls");
		check(text.find("extends Node2D") != String::npos && text.find("ERREUR : Chemin refusé") != String::npos, "les résultats d'outils restent lisibles en texte");
		check(text.find("lire_fichier") != String::npos, "le nom de l'outil utilisé reste lisible");
	}
	{
		const Dictionary request = openai({ user("Lis"), assistant_calls("", { call("a", "lire_fichier", "contenu") }) }, false);
		check_eq(roles(request), "system user assistant user", "appels en cours : la conversation se termine par l'utilisateur");
		check_eq(gemma_problem(request, true), "", "appels en cours : acceptés par le gabarit de Gemma");
	}
	{
		const Dictionary request = openai({ user("Un"), assistant(""), user("Deux"), assistant("Réponse"), assistant("Suite"), user(" "), user("Trois") }, false);
		check_eq(roles(request), "system user assistant user", "messages vides et rôles répétés");
		check_eq(gemma_problem(request, true), "", "messages vides et rôles répétés : acceptés par le gabarit de Gemma");
	}
	{
		// Gabarit qui refuse aussi « system » (Mistral 7B, Gemma 2...) : user / assistant seulement.
		const Dictionary request = openai({ user("Bonjour"), assistant("Salut"), notice("x"), user("Suite"), user("Encore") }, false, false);
		check_eq(roles(request), "user assistant user", "sans outils ni system : user assistant user");
		check_eq(gemma_problem(request, false), "", "sans outils ni system : accepté par un gabarit strict");
		check(S(at(at(request, "messages").as_array()[0].as_dictionary(), "content")).find(SYSTEM) == 0, "sans outils ni system : instructions dans la première demande");
	}
	{
		// Longue conversation aléatoire : la forme reste valide dans les trois modes.
		unsigned seed = 12345;
		const auto next = [&seed]() {
			seed = seed * 1103515245u + 12345u;
			return (seed >> 16) & 0x7fff;
		};
		bool valid_tools = true, valid_gemma = true, valid_strict = true, valid_anthropic = true;
		String detail;
		for (int round = 0; round < 300; round++) {
			std::vector<Message> history;
			const int count = 1 + (int)(next() % 14);
			for (int i = 0; i < count; i++) {
				switch (next() % 7) {
					case 0:
						history.push_back(user(next() % 4 ? "demande" : ""));
						break;
					case 1:
						history.push_back(assistant(next() % 4 ? "réponse" : " "));
						break;
					case 2:
						history.push_back(notice("avis"));
						break;
					case 3:
					case 4: {
						std::vector<ToolCall> calls;
						const int number = 1 + (int)(next() % 3);
						for (int j = 0; j < number; j++) {
							calls.push_back(call(next() % 3 ? "id" + itos(next() % 4) : String(), "lire_fichier", next() % 3 ? "résultat" : "", next() % 5 == 0));
						}
						history.push_back(assistant_calls(next() % 2 ? "texte" : "", calls));
					} break;
					default:
						history.push_back(user("autre demande"));
						break;
				}
			}
			history.push_back(user("dernière demande"));
			String problem = openai_problem(openai(history));
			if (!problem.empty() && valid_tools) {
				valid_tools = false;
				detail = "tirage " + itos(round) + " : " + problem;
			}
			problem = gemma_problem(openai(history, false), true);
			if (!problem.empty() && valid_gemma) {
				valid_gemma = false;
				detail = "tirage " + itos(round) + " : " + problem;
			}
			problem = gemma_problem(openai(history, false, false), false);
			if (!problem.empty() && valid_strict) {
				valid_strict = false;
				detail = "tirage " + itos(round) + " : " + problem;
			}
			problem = anthropic_problem(anthropic(history));
			if (!problem.empty() && valid_anthropic) {
				valid_anthropic = false;
				detail = "tirage " + itos(round) + " : " + problem;
			}
		}
		check(valid_tools, "300 conversations aléatoires : forme valide avec outils", detail);
		check(valid_gemma, "300 conversations aléatoires : acceptées par le gabarit de Gemma", detail);
		check(valid_strict, "300 conversations aléatoires : acceptées par un gabarit sans system", detail);
		check(valid_anthropic, "300 conversations aléatoires : forme valide pour Anthropic", detail);
	}
}

void test_requests_anthropic() {
	section("Requêtes « Messages » d'Anthropic");
	const std::vector<Message> history = {
		user("Lis le joueur"),
		notice("avis"),
		assistant_calls("Je regarde.", { call("toolu_1", "lister_fichiers", "project.mex"), call("toolu_2", "lire_fichier", "") }),
		user("Et ensuite ?"),
		assistant("C'est fait."),
		user("Merci"),
		user("Continue"),
	};
	const Dictionary request = anthropic(history);
	check_eq(S(at(request, "system")), SYSTEM, "instructions dans le champ system");
	check(at(request, "max_tokens").as_int() == 4096, "max_tokens présent");
	check_eq(roles(request), "user assistant user assistant user", "ordre des rôles");
	check_eq(anthropic_problem(request), "", "forme valide");
	const Array tools = at(request, "tools").as_array();
	check(tools.size() == 3 && tools[0].as_dictionary().has(Variant("input_schema")), "outils décrits avec input_schema");
	const Array list = at(request, "messages").as_array();
	const Array results = at(list[2].as_dictionary(), "content").as_array();
	check(results.size() == 3, "résultats d'outils et demande suivante dans le même message user");
	check_eq(S(at(results[0].as_dictionary(), "tool_use_id")), "toolu_1", "premier résultat lié au premier appel");
	check_eq(S(at(results[2].as_dictionary(), "type")), "text", "la demande suit les résultats");
	check(S(at(list[4].as_dictionary(), "content")).find("Merci") != String::npos && S(at(list[4].as_dictionary(), "content")).find("Continue") != String::npos, "deux demandes de suite réunies");
}

void test_responses() {
	section("Réponses JSON");
	Message message;
	String error, stop;
	{
		check(parse("{\"id\":\"chatcmpl-1\",\"object\":\"chat.completion\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"Bonjour !\"},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":3}}", 200, message, error, stop), "réponse texte lue");
		check_eq(message.text, "Bonjour !", "texte de la réponse");
		check(message.calls.empty() && message.kind == Message::ASSISTANT, "réponse sans appel");
		check_eq(stop, "stop", "finish_reason");
	}
	{
		// Appels d'outils, arguments en texte JSON (forme d'OpenAI, de LM Studio et d'Ollama).
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"call_abc\",\"type\":\"function\",\"function\":{\"name\":\"lire_fichier\",\"arguments\":\"{\\\"chemin\\\":\\\"res://joueur.mx\\\",\\\"ligne_debut\\\":3}\"}},{\"id\":\"call_def\",\"type\":\"function\",\"function\":{\"name\":\"lister_fichiers\",\"arguments\":\"{}\"}}]},\"finish_reason\":\"tool_calls\"}]}", 200, message, error, stop), "appels d'outils lus");
		check(message.calls.size() == 2 && message.text.empty(), "deux appels, contenu null accepté");
		if (message.calls.size() == 2) {
			check_eq(message.calls[0].id, "call_abc", "identifiant du premier appel");
			check_eq(message.calls[0].name, "lire_fichier", "nom du premier appel");
			check_eq(S(at(message.calls[0].arguments, "chemin")), "res://joueur.mx", "argument texte");
			check(at(message.calls[0].arguments, "ligne_debut").as_int() == 3, "argument entier");
			check(message.calls[1].arguments.size() == 0 && message.calls[1].state == ToolCall::WAITING, "appel sans argument prêt à s'exécuter");
		}
		check_eq(stop, "tool_calls", "finish_reason des appels");
	}
	{
		// Arguments déjà en objet, identifiant absent, index fourni (certains serveurs locaux).
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"index\":0,\"function\":{\"name\":\"lire_fichier\",\"arguments\":{\"chemin\":\"res://a.mx\"}}},{\"index\":1,\"id\":\"\",\"function\":{\"name\":\"lire_fichier\",\"arguments\":{\"chemin\":\"res://b.mx\"}}}]},\"finish_reason\":\"stop\"}]}", 200, message, error, stop), "arguments en objet lus");
		check(message.calls.size() == 2, "deux appels");
		if (message.calls.size() == 2) {
			check_eq(S(at(message.calls[0].arguments, "chemin")), "res://a.mx", "argument d'un objet");
			check(!message.calls[0].id.empty() && !message.calls[1].id.empty() && message.calls[0].id != message.calls[1].id, "identifiants absents : créés et distincts");
			check(message.calls[0].id.size() == 9, "identifiant créé : 9 caractères alphanumériques");
		}
	}
	{
		// Identifiants en double dans la réponse, et déjà pris dans la conversation.
		std::set<String> used = { "call_0" };
		check(parse_response(false, "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"id\":\"call_0\",\"type\":\"function\",\"function\":{\"name\":\"a\",\"arguments\":\"{}\"}},{\"id\":\"call_1\",\"type\":\"function\",\"function\":{\"name\":\"b\",\"arguments\":\"{}\"}},{\"id\":\"call_1\",\"type\":\"function\",\"function\":{\"name\":\"c\",\"arguments\":\"{}\"}}]}}]}", 200, used, message, error, stop), "identifiants en double lus");
		if (message.calls.size() == 3) {
			check(message.calls[0].id != "call_0", "identifiant déjà pris dans la conversation : renommé");
			check_eq(message.calls[1].id, "call_1", "identifiant libre : conservé");
			check(message.calls[2].id != "call_1" && message.calls[2].id != message.calls[0].id, "identifiant en double dans la réponse : renommé");
			check(used.size() == 4, "les nouveaux identifiants sont retenus");
		} else {
			check(false, "trois appels attendus");
		}
	}
	{
		// Arguments illisibles : l'erreur revient au modèle comme résultat, rien n'est exécuté.
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"id\":\"x1\",\"type\":\"function\",\"function\":{\"name\":\"ecrire_fichier\",\"arguments\":\"{\\\"chemin\\\": \\\"res://a.mx\\\", \\\"contenu\\\": \\\"ext\"}},{\"id\":\"x2\",\"type\":\"function\",\"function\":{\"name\":\"lire_fichier\",\"arguments\":\"[1,2]\"}},{\"id\":\"x3\",\"type\":\"function\",\"function\":{\"name\":\"lister_fichiers\",\"arguments\":\"\"}},{\"id\":\"x4\",\"type\":\"function\",\"function\":{\"name\":\"lister_fichiers\"}},{\"id\":\"x5\",\"type\":\"function\",\"function\":{\"name\":\"lister_fichiers\",\"arguments\":null}}]}}]}", 200, message, error, stop), "arguments illisibles : réponse lue quand même");
		if (message.calls.size() == 5) {
			check(message.calls[0].state == ToolCall::DONE && message.calls[0].is_error && message.calls[0].result.find("JSON") != String::npos, "JSON coupé : appel non exécuté, erreur renvoyée au modèle");
			check(message.calls[1].state == ToolCall::DONE && message.calls[1].is_error, "arguments en tableau : appel non exécuté");
			check(message.calls[2].state == ToolCall::WAITING && message.calls[2].arguments.size() == 0, "arguments \"\" : aucun argument");
			check(message.calls[3].state == ToolCall::WAITING, "arguments absents : aucun argument");
			check(message.calls[4].state == ToolCall::WAITING, "arguments null : aucun argument");
		} else {
			check(false, "cinq appels attendus");
		}
	}
	{
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"id\":\"x1\",\"type\":\"function\",\"function\":{\"name\":\"\",\"arguments\":\"{}\"}}]}}]}", 200, message, error, stop), "appel sans nom : réponse lue");
		check(message.calls.size() == 1 && message.calls[0].state == ToolCall::DONE && message.calls[0].is_error, "appel sans nom : non exécuté");
	}
	{
		// Ancienne forme « function_call ».
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,\"function_call\":{\"name\":\"lister_fichiers\",\"arguments\":\"{}\"}},\"finish_reason\":\"function_call\"}]}", 200, message, error, stop), "function_call lu");
		check(message.calls.size() == 1 && message.calls[0].name == "lister_fichiers" && !message.calls[0].id.empty(), "function_call : un appel avec identifiant");
	}
	{
		// Contenu en liste de morceaux.
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"Bon\"},{\"type\":\"text\",\"text\":\"jour\"}]},\"finish_reason\":\"stop\"}]}", 200, message, error, stop), "contenu en morceaux lu");
		check_eq(message.text, "Bonjour", "morceaux réunis");
	}
	{
		// Réflexion d'un modèle local écrite dans la réponse.
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"<think>\\nL'utilisateur dit bonjour.\\n</think>\\n\\nBonjour !\",\"reasoning_content\":\"...\"},\"finish_reason\":\"stop\"}]}", 200, message, error, stop), "réponse avec réflexion lue");
		check_eq(message.text, "Bonjour !", "bloc <think> retiré");
		check_eq(strip_reasoning("<think>a</think><think>b</think> Réponse"), "Réponse", "deux blocs de réflexion");
		check_eq(strip_reasoning("<think>coupé"), "", "réflexion coupée : rien à afficher");
		check_eq(strip_reasoning("Utilise <think> dans ton texte"), "Utilise <think> dans ton texte", "balise au milieu du texte : conservée");
	}
	{
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"   \"},\"finish_reason\":\"stop\"}]}", 200, message, error, stop), "réponse faite d'espaces lue");
		check(message.text.empty(), "réponse faite d'espaces : texte vide");
	}
	{
		check(parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Début\"},\"finish_reason\":\"length\"}]}", 200, message, error, stop), "réponse coupée lue");
		check_eq(stop, "length", "coupure signalée");
	}
	{
		// Format d'Anthropic.
		check(parse("{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"Je regarde.\"},{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"lire_fichier\",\"input\":{\"chemin\":\"res://a.mx\"}}],\"stop_reason\":\"tool_use\"}", 200, message, error, stop, true), "réponse d'Anthropic lue");
		check(message.text == "Je regarde." && message.calls.size() == 1 && message.calls[0].id == "toolu_1" && S(at(message.calls[0].arguments, "chemin")) == "res://a.mx", "Anthropic : texte et appel");
		check_eq(stop, "tool_use", "Anthropic : stop_reason");
	}
	{
		// Aller-retour : la réponse lue, complétée de ses résultats, redonne une requête valide.
		std::set<String> used;
		std::vector<Message> history = { user("Lis") };
		for (int round = 0; round < 3; round++) {
			// Le même identifiant à chaque tour, comme le font certains serveurs locaux.
			check(parse_response(false, "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"id\":\"call_0\",\"type\":\"function\",\"function\":{\"name\":\"lire_fichier\",\"arguments\":\"{}\"}},{\"type\":\"function\",\"function\":{\"name\":\"lister_fichiers\",\"arguments\":\"{}\"}}]}}]}", 200, used, message, error, stop), "aller-retour : réponse lue");
			for (ToolCall &item : message.calls) {
				item.result = "résultat";
				item.state = ToolCall::DONE;
			}
			history.push_back(message);
		}
		check_eq(openai_problem(openai(history)), "", "aller-retour : requête suivante valide, identifiants uniques");
		check_eq(gemma_problem(openai(history, false), true), "", "aller-retour : même conversation acceptée sans outils");
	}
}

void test_errors() {
	section("Erreurs HTTP et réponses inattendues");
	Message message;
	String error, stop;
	{
		// LM Studio, adresse sans /v1 : HTTP 200 et un champ « error » en texte.
		check(!parse("{\"error\":\"Unexpected endpoint or method. (POST /chat/completions)\"}", 200, message, error, stop), "LM Studio, mauvais chemin : refus reconnu malgré HTTP 200");
		check_eq(error, "Unexpected endpoint or method. (POST /chat/completions)", "LM Studio, mauvais chemin : message");
		check(classify_error(200, error) == ERROR_ENDPOINT, "LM Studio, mauvais chemin : classé « adresse »");
		std::vector<String> names;
		check(!parse_models("{\"error\":\"Unexpected endpoint or method. (GET /models)\"}", 200, names, error), "LM Studio, GET /models : refus reconnu");
		check(classify_error(200, error) == ERROR_ENDPOINT, "LM Studio, GET /models : classé « adresse »");
	}
	{
		// LM Studio, gabarit de Gemma.
		const char *body = "{\"error\":\"Error rendering prompt with jinja template: \\\"Conversation roles must alternate user/assistant/user/assistant/...\\\".\\n\\nThis is usually an issue with the model's prompt template. If you are using a popular model, you can try to search the model under lmstudio-community, which will have fixed prompt templates.\"}";
		check(!parse(body, 400, message, error, stop), "erreur Jinja : refus reconnu");
		check(error.find("Conversation roles must alternate") != String::npos, "erreur Jinja : message transmis");
		check(classify_error(400, error) == ERROR_TEMPLATE, "erreur Jinja : classée « gabarit »");
		check(classify_error(400, "Jinja Exception: Conversation roles must alternate user/assistant/user/assistant") == ERROR_TEMPLATE, "« Jinja Exception » : classée « gabarit »");
		check(classify_error(500, "Error rendering prompt with jinja template: \"Unknown test: sequence\"") == ERROR_TEMPLATE, "autre erreur de gabarit");
	}
	{
		// Ollama, modèle sans outils (réponse réelle pour gemma3).
		const char *body = "{\"error\":{\"message\":\"registry.ollama.ai/library/gemma3:1b does not support tools\",\"type\":\"api_error\",\"param\":null,\"code\":null}}";
		check(!parse(body, 400, message, error, stop), "Ollama sans outils : refus reconnu");
		check_eq(error, "registry.ollama.ai/library/gemma3:1b does not support tools", "Ollama sans outils : message");
		check(classify_error(400, error) == ERROR_NO_TOOLS, "Ollama sans outils : classé « pas d'outils »");
	}
	check(classify_error(404, "No endpoints found that support tool use. To learn more about provider routing, visit: https://openrouter.ai/docs") == ERROR_NO_TOOLS, "OpenRouter sans outils");
	check(classify_error(400, "`tool calling` is not supported with this model") == ERROR_NO_TOOLS, "Groq sans outils");
	check(classify_error(400, "Function calling is not enabled for models/gemma-3-27b-it") == ERROR_NO_TOOLS, "Gemini sans outils");
	check(classify_error(400, "tools is not supported in this model") == ERROR_NO_TOOLS, "autre formulation sans outils");
	check(classify_error(400, "Only user and assistant roles are supported!") == ERROR_SYSTEM_ROLE, "gabarit sans rôle system");
	check(classify_error(400, "System role not supported") == ERROR_SYSTEM_ROLE, "rôle system refusé");
	check(classify_error(400, "Developer instruction is not enabled for models/gemma-3-27b-it") == ERROR_SYSTEM_ROLE, "Gemini, instructions système refusées");
	check(classify_error(400, "The number of tokens to keep from the initial prompt is greater than the context length") == ERROR_CONTEXT, "contexte dépassé (LM Studio)");
	check(classify_error(400, "This model's maximum context length is 8192 tokens") == ERROR_CONTEXT, "contexte dépassé (OpenAI)");
	check(classify_error(404, "model 'gemma3:9b' not found") == ERROR_MODEL, "modèle inconnu (Ollama)");
	check(classify_error(404, "The model `gpt-9` does not exist or you do not have access to it.") == ERROR_MODEL, "modèle inconnu (OpenAI)");
	check(classify_error(400, "No models loaded. Please load a model in the developer page") == ERROR_MODEL, "aucun modèle chargé (LM Studio)");
	check(classify_error(401, "Incorrect API key provided") == ERROR_AUTH, "clé refusée");
	check(classify_error(429, "Rate limit reached") == ERROR_RATE, "limite de débit");
	check(classify_error(404, "404 page not found") == ERROR_ENDPOINT, "404 sans autre précision");
	check(classify_error(405, "Method Not Allowed") == ERROR_ENDPOINT, "405");
	check(classify_error(500, "Internal error") == ERROR_OTHER, "erreur interne");
	{
		// Ollama, mauvais chemin : texte brut.
		check(!parse("404 page not found", 404, message, error, stop), "404 en texte brut : refus reconnu");
		check_eq(error, "404 page not found", "404 en texte brut : message");
	}
	{
		check(!parse("<!DOCTYPE html><html><head><title>502 Bad Gateway</title></head><body><center><h1>502 Bad Gateway</h1></center></body></html>", 502, message, error, stop), "page HTML : refus reconnu");
		check(error.find("502 Bad Gateway") != String::npos && error.find("<h1>") == String::npos, "page HTML : titre seul", error);
	}
	{
		check(!parse("", 200, message, error, stop), "réponse vide : refus reconnu");
		check(!error.empty(), "réponse vide : message");
		check(!parse("data: {\"choices\":[]}", 200, message, error, stop), "flux au lieu de JSON : refus reconnu");
		check(error.find("illisible") != String::npos, "flux au lieu de JSON : message", error);
		check(!parse("[1,2,3]", 200, message, error, stop), "tableau JSON : refus reconnu");
		check(!parse("{\"choices\":[]}", 200, message, error, stop), "aucun choix : refus reconnu");
		check(!parse("{\"object\":\"list\"}", 200, message, error, stop), "objet sans choix : refus reconnu");
		check(!parse("{}", 500, message, error, stop), "HTTP 500 sans explication : refus reconnu");
		check(!error.empty(), "HTTP 500 sans explication : message");
		// Une erreur HTTP l'emporte, même si le corps ressemble à une réponse.
		check(!parse("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"x\"}}]}", 500, message, error, stop), "HTTP 500 avec un corps de réponse : refus reconnu");
	}
	{
		check(!parse("{\"error\":{\"message\":\"Incorrect API key provided: sk-xxx.\",\"type\":\"invalid_request_error\",\"param\":null,\"code\":\"invalid_api_key\"}}", 401, message, error, stop), "erreur d'OpenAI : refus reconnu");
		check_eq(error, "Incorrect API key provided: sk-xxx.", "erreur d'OpenAI : message");
		check(!parse("{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key\"}}", 401, message, error, stop, true), "erreur d'Anthropic : refus reconnu");
		check_eq(error, "invalid x-api-key", "erreur d'Anthropic : message");
		check(!parse("{\"detail\":\"Not Found\"}", 404, message, error, stop), "erreur FastAPI : refus reconnu");
		check_eq(error, "Not Found", "erreur FastAPI : message");
		check(!parse("{\"detail\":[{\"loc\":[\"body\",\"messages\"],\"msg\":\"field required\",\"type\":\"value_error.missing\"}]}", 422, message, error, stop), "erreur de validation : refus reconnu");
		check_eq(error, "field required", "erreur de validation : message");
		check(!parse("{\"message\":\"Model is overloaded\"}", 503, message, error, stop), "erreur dans « message » : refus reconnu");
		check_eq(error, "Model is overloaded", "erreur dans « message » : texte");
		check(!parse("{\"error\":{\"message\":\"Provider returned error\",\"code\":400,\"metadata\":{\"raw\":\"{\\\"error\\\":\\\"tools is not supported\\\"}\",\"provider_name\":\"X\"}}}", 400, message, error, stop), "erreur d'OpenRouter : refus reconnu");
		check(error.find("Provider returned error") != String::npos && error.find("tools is not supported") != String::npos, "erreur d'OpenRouter : détail du fournisseur joint", error);
		check(classify_error(400, error) == ERROR_NO_TOOLS, "erreur d'OpenRouter : classée d'après le détail");
	}
	{
		// Une réponse normale qui contient « error »: null n'est pas une erreur.
		check(parse("{\"error\":null,\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}", 200, message, error, stop), "« error »: null ignoré");
		check_eq(extract_error("{\"choices\":[{\"message\":{\"content\":\"le message dit error\"}}],\"message\":\"x\"}", 200), "", "réponse normale : aucune erreur extraite");
	}
}

void test_models() {
	section("Liste des modèles");
	std::vector<String> names;
	String error;
	check(parse_models("{\"data\":[{\"id\":\"qwen2.5-0.5b-instruct\",\"object\":\"model\",\"owned_by\":\"organization_owner\"},{\"id\":\"gemma-3-1b-it\",\"object\":\"model\"},{\"id\":\"gemma-3-1b-it\",\"object\":\"model\"}],\"object\":\"list\"}", 200, names, error), "liste de LM Studio et d'OpenAI");
	check(names.size() == 2 && names[0] == "gemma-3-1b-it" && names[1] == "qwen2.5-0.5b-instruct", "noms triés, sans doublon");
	check(parse_models("{\"object\":\"list\",\"data\":[{\"id\":\"gemma3:1b\",\"object\":\"model\",\"created\":1,\"owned_by\":\"library\"}]}", 200, names, error) && names.size() == 1 && names[0] == "gemma3:1b", "liste d'Ollama (/v1/models)");
	check(parse_models("{\"models\":[{\"name\":\"models/gemini-pro\"},{\"name\":\"qwen3:0.6b\",\"model\":\"qwen3:0.6b\"}]}", 200, names, error) && names.size() == 2 && names[0] == "gemini-pro", "liste « models » (Gemini, /api/tags)");
	check(parse_models("[{\"id\":\"a\"},\"b\"]", 200, names, error) && names.size() == 2, "liste en tableau");
	check(parse_models("{\"data\":[{\"id\":\"gemma-3-1b-it\",\"object\":\"model\"},{\"id\":\"text-embedding-nomic-embed-text-v1.5\",\"object\":\"model\"}],\"object\":\"list\"}", 200, names, error) && names.size() == 1 && names[0] == "gemma-3-1b-it", "liste de LM Studio : le modèle de plongement est écarté");
	check(parse_models("{\"models\":[{\"type\":\"embedding\",\"key\":\"bge-m3\"},{\"type\":\"llm\",\"key\":\"qwen3-4b\"}]}", 200, names, error) && names.size() == 1 && names[0] == "qwen3-4b", "liste : type « embedding » écarté");
	check(parse_models("{\"data\":[{\"id\":\"text-embedding-3-small\"}]}", 200, names, error) && names.size() == 1, "liste : un modèle de plongement seul reste proposé");
	check(!parse_models("{\"data\":[],\"object\":\"list\"}", 200, names, error) && !error.empty(), "liste vide : signalée");
	check(!parse_models("404 page not found", 404, names, error) && error == "404 page not found", "liste : 404 en texte brut");
	check(!parse_models("{\"error\":{\"message\":\"Incorrect API key provided\"}}", 401, names, error) && error == "Incorrect API key provided", "liste : clé refusée");
	check(!parse_models("", 200, names, error) && !error.empty(), "liste : réponse vide");
}

void test_capabilities() {
	section("Capacités des modèles locaux");
	// GET /api/v0/models de LM Studio : « capabilities » n'existe que pour les modèles à outils.
	const char *v0 = "{\"data\":["
					 "{\"id\":\"qwen2.5-0.5b-instruct\",\"object\":\"model\",\"type\":\"llm\",\"publisher\":\"lmstudio-community\",\"arch\":\"qwen2\",\"compatibility_type\":\"gguf\",\"quantization\":\"Q4_K_M\",\"state\":\"loaded\",\"max_context_length\":32768,\"loaded_context_length\":8192,\"capabilities\":[\"tool_use\"]},"
					 "{\"id\":\"gemma-3-1b-it\",\"object\":\"model\",\"type\":\"llm\",\"publisher\":\"lmstudio-community\",\"arch\":\"gemma3\",\"compatibility_type\":\"gguf\",\"quantization\":\"Q4_K_M\",\"state\":\"loaded\",\"max_context_length\":32768,\"loaded_context_length\":4096},"
					 "{\"id\":\"gemma-3-4b-it\",\"object\":\"model\",\"type\":\"vlm\",\"state\":\"not-loaded\",\"max_context_length\":131072,\"capabilities\":[]},"
					 "{\"id\":\"text-embedding-nomic-embed-text-v1.5\",\"object\":\"model\",\"type\":\"embeddings\",\"state\":\"not-loaded\",\"max_context_length\":2048}"
					 "],\"object\":\"list\"}";
	int context = -1;
	check(lmstudio_tool_support(v0, "qwen2.5-0.5b-instruct", &context) == TOOLS_YES, "LM Studio v0 : modèle à outils");
	check(context == 8192, "LM Studio v0 : contexte du modèle chargé");
	check(lmstudio_tool_support(v0, "gemma-3-1b-it", &context) == TOOLS_NO, "LM Studio v0 : Gemma 3 sans outils (capabilities absent)");
	check(context == 4096, "LM Studio v0 : contexte de Gemma");
	check(lmstudio_tool_support(v0, "gemma-3-4b-it", &context) == TOOLS_NO, "LM Studio v0 : capabilities vide");
	check(context == 0, "LM Studio v0 : modèle non chargé, contexte inconnu");
	check(lmstudio_tool_support(v0, "GEMMA-3-1B-IT") == TOOLS_NO, "LM Studio v0 : nom sans égard à la casse");
	check(lmstudio_tool_support(v0, "gemma-3-1b-it:2") == TOOLS_NO, "LM Studio v0 : seconde instance du même modèle");
	check(lmstudio_tool_support(v0, "text-embedding-nomic-embed-text-v1.5") == TOOLS_NO, "LM Studio v0 : modèle de plongement");
	check(lmstudio_tool_support(v0, "inconnu") == TOOLS_UNKNOWN, "LM Studio v0 : modèle absent de la liste");
	check(lmstudio_tool_support("{\"data\":[{\"id\":\"gemma-3-1b-it\",\"type\":\"llm\",\"state\":\"loaded\"}]}", "gemma-3-1b-it") == TOOLS_UNKNOWN, "LM Studio v0 : aucun modèle ne déclare de capacité, on ne conclut pas");
	// GET /api/v1/models de LM Studio 0.4.
	const char *v1 = "{\"models\":["
					 "{\"type\":\"llm\",\"publisher\":\"lmstudio-community\",\"key\":\"qwen2.5-0.5b-instruct\",\"display_name\":\"Qwen2.5 0.5B Instruct\",\"loaded_instances\":[{\"id\":\"qwen2.5-0.5b-instruct\",\"config\":{\"context_length\":8192}}],\"max_context_length\":32768,\"format\":\"gguf\",\"capabilities\":{\"vision\":false,\"trained_for_tool_use\":true}},"
					 "{\"type\":\"llm\",\"publisher\":\"lmstudio-community\",\"key\":\"gemma-3-1b-it\",\"display_name\":\"Gemma 3 1B\",\"loaded_instances\":[{\"id\":\"gemma-perso\",\"config\":{\"context_length\":4096}}],\"max_context_length\":32768,\"format\":\"gguf\",\"capabilities\":{\"vision\":false,\"trained_for_tool_use\":false}},"
					 "{\"type\":\"embedding\",\"publisher\":\"nomic-ai\",\"key\":\"text-embedding-nomic-embed-text-v1.5\",\"loaded_instances\":[],\"max_context_length\":2048,\"format\":\"gguf\"}"
					 "]}";
	check(lmstudio_tool_support(v1, "qwen2.5-0.5b-instruct", &context) == TOOLS_YES && context == 8192, "LM Studio v1 : modèle à outils et contexte");
	check(lmstudio_tool_support(v1, "gemma-3-1b-it", &context) == TOOLS_NO && context == 4096, "LM Studio v1 : Gemma 3 sans outils");
	check(lmstudio_tool_support(v1, "gemma-perso") == TOOLS_NO, "LM Studio v1 : nom donné à l'instance chargée");
	check(lmstudio_tool_support(v1, "text-embedding-nomic-embed-text-v1.5") == TOOLS_NO, "LM Studio v1 : modèle de plongement");
	check(lmstudio_tool_support(v1, "inconnu") == TOOLS_UNKNOWN, "LM Studio v1 : modèle absent");
	check(lmstudio_tool_support("{\"error\":\"Unexpected endpoint or method. (GET /api/v1/models)\"}", "x") == TOOLS_UNKNOWN, "LM Studio : version sans cette API");
	check(lmstudio_tool_support("404 page not found", "x") == TOOLS_UNKNOWN, "autre serveur : réponse illisible");
	// POST /api/show d'Ollama.
	check(ollama_tool_support("{\"license\":\"...\",\"details\":{\"family\":\"gemma3\"},\"capabilities\":[\"completion\"]}") == TOOLS_NO, "Ollama : gemma3 sans outils");
	check(ollama_tool_support("{\"details\":{\"family\":\"qwen2\"},\"capabilities\":[\"completion\",\"tools\"]}") == TOOLS_YES, "Ollama : qwen2.5 avec outils");
	check(ollama_tool_support("{\"details\":{\"family\":\"qwen3\"},\"capabilities\":[\"completion\",\"tools\",\"thinking\"]}") == TOOLS_YES, "Ollama : qwen3 avec outils");
	check(ollama_tool_support("{\"details\":{\"family\":\"llama\"}}") == TOOLS_UNKNOWN, "Ollama ancien : capacités non déclarées");
	check(ollama_tool_support("{\"error\":\"model 'x' not found\"}") == TOOLS_UNKNOWN, "Ollama : modèle inconnu");
	check(ollama_tool_support("{\"error\":\"Unexpected endpoint or method. (POST /api/show)\"}") == TOOLS_UNKNOWN, "LM Studio interrogé comme Ollama");
}

void test_misc() {
	section("Identifiants et divers");
	std::set<String> used;
	check_eq(unique_call_id("call_1", used), "call_1", "identifiant proposé libre : conservé");
	const String second = unique_call_id("call_1", used);
	check(second != "call_1" && second.size() == 9, "identifiant déjà pris : remplacé");
	const String third = unique_call_id("", used);
	check(!third.empty() && third != second, "identifiant vide : créé");
	bool alphanumeric = true;
	for (const char c : third) {
		alphanumeric = alphanumeric && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'));
	}
	check(alphanumeric, "identifiant créé : lettres et chiffres seulement");
	std::set<String> many;
	for (int i = 0; i < 2000; i++) {
		unique_call_id("", many);
	}
	check(many.size() == 2000, "2000 identifiants créés, tous distincts");
	check(estimate_tokens("") == 0 && estimate_tokens(String(3000, 'a')) == 1000, "estimation du nombre de jetons");
}

} // namespace

int main() {
	test_urls();
	test_requests_openai();
	test_requests_without_tools();
	test_requests_anthropic();
	test_responses();
	test_errors();
	test_models();
	test_capabilities();
	test_misc();
	printf("BILAN AGENT : %d vérifications réussies, %d échec(s)\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
