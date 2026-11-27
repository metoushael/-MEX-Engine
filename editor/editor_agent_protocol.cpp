// Protocole de MEX Agent : adresses, requêtes, réponses, erreurs, capacités des modèles.
// Aucune dépendance envers l'éditeur ni le réseau (voir editor_agent_protocol.h).
#include "editor_agent_protocol.h"

#include "core/io/json.h"
#include "core/string/ustring.h"

#include <algorithm>

namespace EditorAgentProtocol {

namespace {

typedef EditorAgent::Message Message;
typedef EditorAgent::ToolCall ToolCall;

String _get(const Dictionary &p_dictionary, const char *p_key) {
	const Variant value = p_dictionary.get(Variant(p_key), Variant());
	return value.get_type() == Variant::NIL ? String() : value.as_string();
}

Dictionary _dict(const Dictionary &p_dictionary, const char *p_key) {
	const Variant value = p_dictionary.get(Variant(p_key), Variant());
	return value.get_type() == Variant::DICTIONARY ? value.as_dictionary() : Dictionary();
}

Array _array(const Dictionary &p_dictionary, const char *p_key) {
	const Variant value = p_dictionary.get(Variant(p_key), Variant());
	return value.get_type() == Variant::ARRAY ? value.as_array() : Array();
}

bool _contains(const String &p_text, const char *p_what) {
	return p_text.find(p_what) != String::npos;
}

bool _is_digit(char p_c) {
	return p_c >= '0' && p_c <= '9';
}

// « v1 », « v1beta », « v0 », « v4 »...
bool _is_version_segment(const String &p_segment) {
	return p_segment.size() >= 2 && (p_segment[0] == 'v' || p_segment[0] == 'V') && _is_digit(p_segment[1]);
}

// Hôte et port d'une autorité « utilisateur@hôte:port » ou « [::1]:port ».
void _split_authority(const String &p_authority, String &r_host, String &r_port) {
	String rest = p_authority;
	const size_t at = rest.rfind('@');
	if (at != String::npos) {
		rest = rest.substr(at + 1);
	}
	r_port.clear();
	if (!rest.empty() && rest[0] == '[') {
		const size_t end = rest.find(']');
		r_host = rest.substr(1, end == String::npos ? String::npos : end - 1);
		if (end != String::npos && end + 1 < rest.size() && rest[end + 1] == ':') {
			r_port = rest.substr(end + 2);
		}
	} else {
		const size_t colon = rest.rfind(':');
		if (colon != String::npos && rest.find(':') == colon) {
			r_host = rest.substr(0, colon);
			r_port = rest.substr(colon + 1);
		} else {
			r_host = rest; // pas de port, ou adresse IPv6 sans crochets
		}
	}
	r_host = str_to_lower(r_host);
}

bool _is_local_host(const String &p_host) {
	if (p_host.empty()) {
		return false;
	}
	if (p_host == "localhost" || p_host == "::1" || p_host == "0.0.0.0" || p_host == "host.docker.internal") {
		return true;
	}
	for (const char *suffix : { ".localhost", ".local", ".lan", ".home", ".internal" }) {
		if (str_ends_with(p_host, suffix)) {
			return true;
		}
	}
	if (str_begins_with(p_host, "127.") || str_begins_with(p_host, "10.") || str_begins_with(p_host, "192.168.") || str_begins_with(p_host, "169.254.")) {
		return true;
	}
	if (str_begins_with(p_host, "100.")) {
		const int second = atoi(p_host.c_str() + 4); // 100.64.0.0/10 : réseaux privés virtuels
		return second >= 64 && second <= 127;
	}
	if (str_begins_with(p_host, "172.")) {
		const int second = atoi(p_host.c_str() + 4);
		return second >= 16 && second <= 31;
	}
	if (str_begins_with(p_host, "fe80:") || str_begins_with(p_host, "fc") || str_begins_with(p_host, "fd")) {
		return p_host.find(':') != String::npos; // IPv6 locale
	}
	// Nom sans point : une machine du réseau local (« monpc », « serveur-ia »).
	return p_host.find('.') == String::npos && p_host.find(':') == String::npos;
}

// Découpe « schéma://autorité/chemin ». Renvoie false si le texte n'a pas d'autorité.
bool _split_url(const String &p_text, String &r_scheme, String &r_authority, String &r_path) {
	String url = str_strip(p_text);
	// Guillemets, chevrons ou accents graves d'un copier-coller.
	while (!url.empty() && (url.front() == '"' || url.front() == '\'' || url.front() == '<' || url.front() == '`')) {
		url.erase(url.begin());
	}
	while (!url.empty() && (url.back() == '"' || url.back() == '\'' || url.back() == '>' || url.back() == '`')) {
		url.pop_back();
	}
	url = str_strip(url);
	for (char &c : url) {
		if (c == '\\') {
			c = '/';
		}
	}
	r_scheme.clear();
	const size_t separator = url.find("://");
	if (separator != String::npos) {
		r_scheme = str_to_lower(url.substr(0, separator));
		url = url.substr(separator + 3);
	}
	while (!url.empty() && url.front() == '/') {
		url.erase(url.begin());
	}
	const size_t extra = url.find_first_of("?#");
	if (extra != String::npos) {
		url.resize(extra);
	}
	const size_t slash = url.find('/');
	r_authority = url.substr(0, slash);
	r_path = slash == String::npos ? String() : url.substr(slash);
	// Une espace dans l'autorité : ce n'est pas une adresse.
	if (r_authority.empty() || r_authority.find(' ') != String::npos) {
		return false;
	}
	return true;
}

bool _blank(const String &p_text) {
	return str_strip(p_text).empty();
}

String _result_text(const ToolCall &p_call) {
	if (_blank(p_call.result)) {
		return p_call.is_error ? "ERREUR : (aucun détail)" : "(aucun résultat)";
	}
	return p_call.is_error ? "ERREUR : " + p_call.result : p_call.result;
}

// Un tour de la conversation, une fois les messages de l'éditeur écartés, les messages vides
// retirés et les messages de même rôle réunis.
struct Entry {
	bool user = true;
	String text;
	const Message *calls = nullptr; // réponse du modèle qui porte des appels d'outils
};

void _add_entry(std::vector<Entry> &r_entries, bool p_user, const String &p_text, const Message *p_calls) {
	if (!r_entries.empty() && r_entries.back().user == p_user && !r_entries.back().calls) {
		Entry &last = r_entries.back();
		if (!p_calls) {
			// Deux messages de même rôle de suite : un seul message. C'est le cas d'une demande
			// restée sans réponse (erreur, interruption) suivie d'une nouvelle demande.
			if (!p_text.empty()) {
				last.text += (last.text.empty() ? "" : "\n\n") + p_text;
			}
			return;
		}
		// Texte du modèle suivi d'une réponse avec outils : le texte passe dans celle-ci.
		Entry merged;
		merged.user = false;
		merged.text = last.text + (p_text.empty() || last.text.empty() ? "" : "\n\n") + p_text;
		merged.calls = p_calls;
		last = merged;
		return;
	}
	Entry entry;
	entry.user = p_user;
	entry.text = p_text;
	entry.calls = p_calls;
	r_entries.push_back(entry);
}

// p_flatten : les appels d'outils et leurs résultats deviennent du texte (modèle sans outils).
std::vector<Entry> _entries(const std::vector<Message> &p_messages, bool p_flatten) {
	std::vector<Entry> entries;
	for (const Message &message : p_messages) {
		if (message.kind == Message::NOTICE) {
			continue; // message de l'éditeur : jamais envoyé au modèle
		}
		const String text = _blank(message.text) ? String() : message.text;
		if (message.kind == Message::USER) {
			if (!text.empty()) {
				_add_entry(entries, true, text, nullptr);
			}
			continue;
		}
		if (message.calls.empty()) {
			if (!text.empty()) {
				_add_entry(entries, false, text, nullptr);
			}
			continue;
		}
		if (!p_flatten) {
			_add_entry(entries, false, text, &message);
			continue;
		}
		String said = text;
		String results;
		for (const ToolCall &call : message.calls) {
			said += (said.empty() ? "" : "\n") + String("[Outil utilisé : ") + call.name + "]";
			results += (results.empty() ? "" : "\n\n") + String("[Résultat de l'outil ") + call.name + "]\n" + _result_text(call);
		}
		_add_entry(entries, false, said, nullptr);
		_add_entry(entries, true, results, nullptr);
	}
	// Une conversation commence par l'utilisateur.
	while (!entries.empty() && !entries.front().user) {
		entries.erase(entries.begin());
	}
	return entries;
}

Dictionary _message(const char *p_role, const Variant &p_content) {
	Dictionary message;
	message[Variant("role")] = Variant(p_role);
	message[Variant("content")] = p_content;
	return message;
}

Dictionary _build_anthropic(const String &p_model, const String &p_system, const std::vector<Message> &p_messages, const Array &p_definitions, int p_max_tokens) {
	// API « Messages » d'Anthropic : le texte et les appels d'outils sont des blocs de
	// contenu ; les résultats reviennent dans un message de l'utilisateur.
	Dictionary request;
	request[Variant("model")] = Variant(p_model);
	request[Variant("max_tokens")] = Variant(p_max_tokens);
	if (!_blank(p_system)) {
		request[Variant("system")] = Variant(p_system);
	}
	Array tools;
	for (int i = 0; i < p_definitions.size(); i++) {
		const Dictionary definition = p_definitions[i].as_dictionary();
		Dictionary tool;
		tool[Variant("name")] = definition.get(Variant("name"), Variant());
		tool[Variant("description")] = definition.get(Variant("description"), Variant());
		tool[Variant("input_schema")] = definition.get(Variant("parameters"), Variant());
		tools.push_back(Variant(tool));
	}
	Array list;
	std::set<String> ids;
	bool results_pending = false; // le dernier message porte des résultats d'outils
	for (const Entry &entry : _entries(p_messages, false)) {
		if (entry.user) {
			if (results_pending) {
				// Demande écrite juste après des résultats d'outils : même message « user ».
				Dictionary block;
				block[Variant("type")] = Variant("text");
				block[Variant("text")] = Variant(entry.text);
				Dictionary last = list[list.size() - 1].as_dictionary();
				Array content = last.get(Variant("content"), Variant(Array())).as_array();
				content.push_back(Variant(block));
				last[Variant("content")] = Variant(content);
				list[list.size() - 1] = Variant(last);
			} else {
				list.push_back(Variant(_message("user", Variant(entry.text))));
			}
			results_pending = false;
			continue;
		}
		Array content;
		if (!entry.text.empty()) {
			Dictionary block;
			block[Variant("type")] = Variant("text");
			block[Variant("text")] = Variant(entry.text);
			content.push_back(Variant(block));
		}
		Array results;
		if (entry.calls) {
			for (const ToolCall &call : entry.calls->calls) {
				const String id = unique_call_id(call.id, ids);
				Dictionary block;
				block[Variant("type")] = Variant("tool_use");
				block[Variant("id")] = Variant(id);
				block[Variant("name")] = Variant(call.name);
				block[Variant("input")] = Variant(call.arguments);
				content.push_back(Variant(block));
				Dictionary result;
				result[Variant("type")] = Variant("tool_result");
				result[Variant("tool_use_id")] = Variant(id);
				result[Variant("content")] = Variant(_blank(call.result) ? String("(aucun résultat)") : call.result);
				if (call.is_error) {
					result[Variant("is_error")] = Variant(true);
				}
				results.push_back(Variant(result));
			}
		}
		list.push_back(Variant(_message("assistant", Variant(content))));
		results_pending = results.size() > 0;
		if (results_pending) {
			list.push_back(Variant(_message("user", Variant(results))));
		}
	}
	request[Variant("tools")] = Variant(tools);
	request[Variant("messages")] = Variant(list);
	return request;
}

Dictionary _build_openai(const String &p_model, const String &p_system, const std::vector<Message> &p_messages, const Array &p_definitions, const Dialect &p_dialect) {
	// Format « chat completions » d'OpenAI, repris par OpenRouter, Mistral, Groq, Gemini,
	// DeepSeek, Ollama, LM Studio, llama.cpp...
	Dictionary request;
	request[Variant("model")] = Variant(p_model);
	request[Variant("stream")] = Variant(false);
	Array list;
	bool system_pending = !_blank(p_system);
	if (system_pending && p_dialect.system_role) {
		list.push_back(Variant(_message("system", Variant(p_system))));
		system_pending = false;
	}
	std::set<String> ids;
	bool after_results = false; // le dernier message est un résultat d'outil
	for (const Entry &entry : _entries(p_messages, !p_dialect.tools)) {
		if (entry.user) {
			if (after_results) {
				// Demande écrite juste après des résultats d'outils (interruption, requête en
				// erreur) : plusieurs services et gabarits exigent une réponse du modèle entre
				// les deux (Mistral : « Unexpected role 'user' after role 'tool' »).
				list.push_back(Variant(_message("assistant", Variant("(Réponse interrompue avant la fin.)"))));
				after_results = false;
			}
			String text = entry.text;
			if (system_pending) {
				// Gabarit sans rôle « system » : les instructions ouvrent la première demande.
				text = p_system + "\n\n---\n\n" + text;
				system_pending = false;
			}
			list.push_back(Variant(_message("user", Variant(text))));
			continue;
		}
		if (!entry.calls) {
			list.push_back(Variant(_message("assistant", Variant(entry.text))));
			after_results = false;
			continue;
		}
		after_results = true;
		// Sans texte : null pour les services distants (forme de la documentation d'OpenAI),
		// chaîne vide pour les serveurs locaux, dont les gabarits Jinja ne lisent pas null.
		Dictionary message = _message("assistant", entry.text.empty() && !p_dialect.local ? Variant() : Variant(entry.text));
		Array calls;
		std::vector<String> call_ids;
		for (const ToolCall &call : entry.calls->calls) {
			const String id = unique_call_id(call.id, ids);
			call_ids.push_back(id);
			Dictionary function;
			function[Variant("name")] = Variant(call.name);
			function[Variant("arguments")] = Variant(JSON::stringify(Variant(call.arguments)));
			Dictionary item;
			item[Variant("id")] = Variant(id);
			item[Variant("type")] = Variant("function");
			item[Variant("function")] = Variant(function);
			// Ce que le service a joint à l'appel lui revient tel quel (signature de réflexion
			// de Gemini) ; un autre service ou un autre modèle ne le reçoit pas.
			const bool own_extra = call.extra.size() > 0 && call.extra_origin == p_dialect.origin;
			if (own_extra) {
				item[Variant("extra_content")] = Variant(call.extra);
			}
			if (p_dialect.thought_signatures && calls.size() == 0 && (!own_extra || _get(_dict(call.extra, "google"), "thought_signature").empty())) {
				// Étape que ce modèle n'a pas signée : seul le premier appel d'une étape porte
				// une signature, les appels parallèles qui suivent n'en ont pas.
				Dictionary google = own_extra ? _dict(call.extra, "google").duplicate() : Dictionary();
				google[Variant("thought_signature")] = Variant(THOUGHT_SIGNATURE_PLACEHOLDER);
				Dictionary extra = own_extra ? call.extra.duplicate() : Dictionary();
				extra[Variant("google")] = Variant(google);
				item[Variant("extra_content")] = Variant(extra);
			}
			calls.push_back(Variant(item));
		}
		message[Variant("tool_calls")] = Variant(calls);
		list.push_back(Variant(message));
		for (size_t i = 0; i < entry.calls->calls.size(); i++) {
			const ToolCall &call = entry.calls->calls[i];
			Dictionary result = _message("tool", Variant(_result_text(call)));
			result[Variant("tool_call_id")] = Variant(call_ids[i]);
			list.push_back(Variant(result));
		}
	}
	if (system_pending) {
		list.push_back(Variant(_message("user", Variant(p_system))));
	}
	if (p_dialect.tools && p_definitions.size() > 0) {
		Array tools;
		for (int i = 0; i < p_definitions.size(); i++) {
			Dictionary tool;
			tool[Variant("type")] = Variant("function");
			tool[Variant("function")] = p_definitions[i];
			tools.push_back(Variant(tool));
		}
		request[Variant("tools")] = Variant(tools);
	}
	request[Variant("messages")] = Variant(list);
	return request;
}

// Texte d'un contenu « chat completions » : une chaîne, ou une liste de morceaux.
String _content_text(const Variant &p_content) {
	if (p_content.get_type() == Variant::STRING) {
		return p_content.as_string();
	}
	String text;
	if (p_content.get_type() == Variant::ARRAY) {
		const Array parts = p_content.as_array();
		for (int i = 0; i < parts.size(); i++) {
			if (parts[i].get_type() == Variant::STRING) {
				text += parts[i].as_string();
			} else if (parts[i].get_type() == Variant::DICTIONARY) {
				const Dictionary part = parts[i].as_dictionary();
				const String type = _get(part, "type");
				if (type.empty() || type == "text" || type == "output_text") {
					text += _get(part, "text");
				}
			}
		}
	}
	return text;
}

// Arguments d'un appel d'outil : un objet, ou le texte JSON d'un objet.
void _read_arguments(const Variant &p_arguments, ToolCall &r_call) {
	if (p_arguments.get_type() == Variant::DICTIONARY) {
		r_call.arguments = p_arguments.as_dictionary();
		return;
	}
	if (p_arguments.get_type() == Variant::NIL) {
		return;
	}
	const String text = str_strip(p_arguments.as_string());
	if (text.empty() || text == "null") {
		return; // aucun argument
	}
	Variant value;
	String error;
	if (JSON::parse(text, value, &error) && value.get_type() == Variant::DICTIONARY) {
		r_call.arguments = value.as_dictionary();
		return;
	}
	// Le modèle saura corriger : l'erreur lui revient comme résultat de l'outil.
	r_call.state = ToolCall::DONE;
	r_call.is_error = true;
	r_call.result = "Arguments illisibles (" + (error.empty() ? String("ce n'est pas un objet JSON") : "JSON invalide : " + error) + "). Rappelle l'outil avec un objet JSON valide.";
}

String _plain_summary(const String &p_body) {
	String text = str_strip(p_body);
	const String lower = str_to_lower(text.substr(0, 2000));
	if (_contains(lower, "<html") || _contains(lower, "<!doctype")) {
		const size_t begin = lower.find("<title>");
		const size_t end = lower.find("</title>");
		if (begin != String::npos && end != String::npos && end > begin + 7) {
			return "page web « " + str_strip(text.substr(begin + 7, end - begin - 7)) + " »";
		}
		return "page web au lieu d'une réponse JSON";
	}
	if (text.size() > 300) {
		text.resize(300);
		while (!text.empty() && ((unsigned char)text.back() & 0xC0) == 0x80) {
			text.pop_back();
		}
		if (!text.empty() && ((unsigned char)text.back() & 0x80)) {
			text.pop_back();
		}
		text += "...";
	}
	return text;
}

} // namespace

// ---------------------------------------------------------------------------
// Adresses

String normalize_url(const String &p_text) {
	String scheme, authority, path;
	if (!_split_url(p_text, scheme, authority, path)) {
		return String();
	}
	String host, port;
	_split_authority(authority, host, port);
	if (scheme.empty()) {
		scheme = _is_local_host(host) ? "http" : "https";
	}
	if (host == "0.0.0.0") {
		// Adresse d'écoute affichée par certains serveurs : on ne s'y connecte pas.
		authority = "127.0.0.1" + (port.empty() ? String() : ":" + port);
	}
	std::vector<String> segments = str_split(path, "/", false);
	// Adresse complète d'un point d'accès : on garde la base.
	bool removed = true;
	while (removed && !segments.empty()) {
		removed = false;
		const String last = str_to_lower(segments.back());
		if (segments.size() >= 2 && last == "completions" && str_to_lower(segments[segments.size() - 2]) == "chat") {
			segments.pop_back();
			segments.pop_back();
			removed = true;
		} else if (last == "completions" || last == "messages" || last == "models" || last == "responses" || last == "embeddings") {
			segments.pop_back();
			removed = true;
		}
	}
	// API propre à Ollama (/api/chat, /api/tags...) : MEX Agent parle à son API compatible
	// avec OpenAI, sous /v1.
	if (!segments.empty() && str_to_lower(segments[0]) == "api") {
		bool ollama = segments.size() == 1 && port == "11434";
		if (segments.size() == 2) {
			const String second = str_to_lower(segments[1]);
			for (const char *name : { "chat", "generate", "tags", "show", "version", "ps" }) {
				ollama = ollama || second == name;
			}
		}
		if (ollama) {
			segments.clear();
		}
	}
	// « /v1/v1 » : une seule fois.
	for (size_t i = 1; i < segments.size();) {
		if (_is_version_segment(segments[i]) && str_to_lower(segments[i]) == str_to_lower(segments[i - 1])) {
			segments.erase(segments.begin() + (long)i);
		} else {
			i++;
		}
	}
	bool versioned = false;
	for (const String &segment : segments) {
		versioned = versioned || _is_version_segment(segment);
	}
	if (!versioned && _is_local_host(host)) {
		segments.push_back("v1");
	}
	return scheme + "://" + authority + (segments.empty() ? String() : "/" + str_join(segments, "/"));
}

String server_root(const String &p_base_url) {
	String scheme, authority, path;
	if (!_split_url(p_base_url, scheme, authority, path)) {
		return String();
	}
	return (scheme.empty() ? String("http") : scheme) + "://" + authority;
}

bool is_local_url(const String &p_url) {
	String scheme, authority, path, host, port;
	if (!_split_url(p_url, scheme, authority, path)) {
		return false;
	}
	_split_authority(authority, host, port);
	return _is_local_host(host);
}

// ---------------------------------------------------------------------------
// Requêtes

Dictionary build_request(bool p_anthropic_format, const String &p_model, const String &p_system, const std::vector<Message> &p_messages, const Array &p_definitions, int p_max_tokens, const Dialect &p_dialect) {
	if (p_anthropic_format) {
		return _build_anthropic(p_model, p_system, p_messages, p_definitions, p_max_tokens);
	}
	return _build_openai(p_model, p_system, p_messages, p_definitions, p_dialect);
}

// ---------------------------------------------------------------------------
// Réponses

String extract_error(const String &p_body, int p_http_status) {
	const bool failed = p_http_status >= 400;
	Variant parsed;
	const bool readable = JSON::parse(p_body, parsed);
	// Google enveloppe ses erreurs dans un tableau : [{ "error": { "code", "message", "status" } }].
	if (readable && parsed.get_type() == Variant::ARRAY && parsed.as_array().size() > 0 && parsed.as_array()[0].get_type() == Variant::DICTIONARY && parsed.as_array()[0].as_dictionary().has(Variant("error"))) {
		const Variant first = parsed.as_array()[0];
		parsed = first;
	}
	if (!readable || parsed.get_type() != Variant::DICTIONARY) {
		if (!failed) {
			return String();
		}
		const String summary = _plain_summary(p_body);
		return summary.empty() ? String("réponse vide") : summary;
	}
	const Dictionary root = parsed.as_dictionary();
	const Variant error = root.get(Variant("error"), Variant());
	String text;
	if (error.get_type() == Variant::DICTIONARY) {
		const Dictionary details = error.as_dictionary();
		text = _get(details, "message");
		if (text.empty()) {
			text = _get(details, "msg");
		}
		if (text.empty() && details.size() > 0) {
			text = JSON::stringify(error);
		}
		// OpenRouter joint la réponse du fournisseur d'origine.
		const String raw = _get(_dict(details, "metadata"), "raw");
		if (!raw.empty() && raw != text) {
			text += " (" + _plain_summary(raw) + ")";
		}
	} else if (error.get_type() == Variant::STRING) {
		text = error.as_string();
	} else if (error.get_type() != Variant::NIL && error.as_bool()) {
		text = JSON::stringify(error);
	}
	if (text.empty()) {
		// FastAPI (vLLM, serveurs maison) : « detail », texte ou liste d'erreurs de validation.
		const Variant detail = root.get(Variant("detail"), Variant());
		if (detail.get_type() == Variant::STRING) {
			text = detail.as_string();
		} else if (detail.get_type() == Variant::ARRAY) {
			const Array items = detail.as_array();
			for (int i = 0; i < items.size(); i++) {
				const String item = items[i].get_type() == Variant::DICTIONARY ? _get(items[i].as_dictionary(), "msg") : items[i].as_string();
				text += (text.empty() || item.empty() ? "" : " ; ") + item;
			}
		}
	}
	const bool answered = root.has(Variant("choices")) || root.has(Variant("content")) || root.has(Variant("data")) || root.has(Variant("models"));
	if (text.empty() && !answered) {
		text = _get(root, "message");
	}
	if (text.empty() && failed && !answered) {
		text = _plain_summary(p_body);
	}
	return str_strip(text);
}

const char *const THOUGHT_SIGNATURE_PLACEHOLDER = "skip_thought_signature_validator";

bool wants_thought_signatures(const String &p_base_url) {
	const String url = str_to_lower(p_base_url);
	const size_t start = url.find("://");
	const size_t host_start = start == String::npos ? 0 : start + 3;
	const String host = url.substr(host_start, url.find('/', host_start) == String::npos ? String::npos : url.find('/', host_start) - host_start);
	return _contains(host, "googleapis.com");
}

ErrorKind classify_error(int p_http_status, const String &p_text) {
	const String text = str_to_lower(p_text);
	if (_contains(text, "unexpected endpoint or method")) {
		return ERROR_ENDPOINT;
	}
	if (_contains(text, "thought_signature") || _contains(text, "thought signature") || _contains(text, "thoughtsignature")) {
		return ERROR_SIGNATURE;
	}
	const bool unsupported = _contains(text, "not support") || _contains(text, "unsupported") || _contains(text, "not enabled") || _contains(text, "no endpoints found that support") || _contains(text, "not available") || _contains(text, "not allowed");
	const bool about_tools = _contains(text, "tool") || _contains(text, "function call");
	// Mistral, Zephyr... : « Only user and assistant roles are supported! »
	if (_contains(text, "only user and assistant roles")) {
		return ERROR_SYSTEM_ROLE;
	}
	if (unsupported && (_contains(text, "system role") || _contains(text, "system prompt") || _contains(text, "system message") || _contains(text, "developer instruction") || _contains(text, "system instruction"))) {
		return ERROR_SYSTEM_ROLE;
	}
	if (unsupported && about_tools) {
		return ERROR_NO_TOOLS;
	}
	if (_contains(text, "jinja") || _contains(text, "roles must alternate") || _contains(text, "chat template") || _contains(text, "raise_exception") || _contains(text, "rendering prompt") || _contains(text, "prompt template")) {
		return ERROR_TEMPLATE;
	}
	for (const char *sign : { "context length", "context window", "context size", "maximum context", "n_ctx", "too many tokens", "tokens to keep", "exceeds the available context", "prompt is too long", "context_length_exceeded", "exceed context", "context overflow", "trying to keep the first" }) {
		if (_contains(text, sign)) {
			return ERROR_CONTEXT;
		}
	}
	if ((_contains(text, "model") && (_contains(text, "not found") || _contains(text, "does not exist") || _contains(text, "not loaded") || _contains(text, "no models loaded") || _contains(text, "failed to load") || _contains(text, "unknown model"))) || _contains(text, "model_not_found")) {
		return ERROR_MODEL;
	}
	if (p_http_status == 401 || p_http_status == 403) {
		return ERROR_AUTH;
	}
	if (p_http_status == 429) {
		return ERROR_RATE;
	}
	if (p_http_status == 404 || p_http_status == 405) {
		return ERROR_ENDPOINT;
	}
	return ERROR_OTHER;
}

String unique_call_id(const String &p_proposed, std::set<String> &p_used_ids) {
	if (!p_proposed.empty() && !p_used_ids.count(p_proposed)) {
		p_used_ids.insert(p_proposed);
		return p_proposed;
	}
	// « mex » et six caractères : neuf caractères alphanumériques, la forme la plus stricte
	// exigée par un service (Mistral).
	static const char ALPHABET[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	for (size_t number = p_used_ids.size() + 1;; number++) {
		String id = "mex";
		size_t value = number;
		char digits[6];
		for (int i = 5; i >= 0; i--) {
			digits[i] = ALPHABET[value % 36];
			value /= 36;
		}
		id.append(digits, 6);
		if (!p_used_ids.count(id)) {
			p_used_ids.insert(id);
			return id;
		}
	}
}

String strip_reasoning(const String &p_text) {
	String text = p_text;
	for (;;) {
		size_t start = 0;
		while (start < text.size() && (unsigned char)text[start] <= ' ') {
			start++;
		}
		const char *tag = nullptr;
		for (const char *candidate : { "think", "thinking", "reasoning", "thought" }) {
			const String open = String("<") + candidate + ">";
			if (text.compare(start, open.size(), open) == 0) {
				tag = candidate;
				break;
			}
		}
		if (!tag) {
			return text;
		}
		const String close = String("</") + tag + ">";
		const size_t end = text.find(close, start);
		if (end == String::npos) {
			return String(); // réflexion coupée avant la réponse
		}
		text = text.substr(end + close.size());
		size_t skip = 0;
		while (skip < text.size() && (unsigned char)text[skip] <= ' ') {
			skip++;
		}
		text = text.substr(skip);
	}
}

int estimate_tokens(const String &p_text) {
	return (int)((p_text.size() + 2) / 3);
}

bool parse_response(bool p_anthropic_format, const String &p_body, int p_http_status, std::set<String> &p_used_ids, Message &r_message, String &r_error, String &r_stop_reason) {
	r_message = Message();
	r_message.kind = Message::ASSISTANT;
	r_error.clear();
	r_stop_reason.clear();
	Variant parsed;
	String json_error;
	if (!JSON::parse(p_body, parsed, &json_error) || parsed.get_type() != Variant::DICTIONARY) {
		if (_blank(p_body)) {
			r_error = "le service a renvoyé une réponse vide";
		} else if (p_http_status >= 400 || !extract_error(p_body, p_http_status).empty()) {
			r_error = extract_error(p_body, p_http_status);
		} else {
			r_error = "réponse illisible (" + (json_error.empty() ? String("ce n'est pas un objet JSON") : json_error) + ") : " + _plain_summary(p_body);
		}
		return false;
	}
	const Dictionary root = parsed.as_dictionary();
	r_error = extract_error(p_body, p_http_status);
	if (r_error.empty() && p_http_status >= 400) {
		r_error = "erreur sans explication du service";
	}
	if (!r_error.empty()) {
		return false;
	}
	if (p_anthropic_format) {
		if (root.get(Variant("content"), Variant()).get_type() != Variant::ARRAY) {
			r_error = "réponse inattendue : " + _plain_summary(p_body);
			return false;
		}
		const Array content = _array(root, "content");
		for (int i = 0; i < content.size(); i++) {
			if (content[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary block = content[i].as_dictionary();
			const String type = _get(block, "type");
			if (type == "text") {
				r_message.text += _get(block, "text");
			} else if (type == "tool_use") {
				ToolCall call;
				call.id = unique_call_id(_get(block, "id"), p_used_ids);
				call.name = _get(block, "name");
				_read_arguments(block.get(Variant("input"), Variant()), call);
				r_message.calls.push_back(call);
			}
		}
		r_stop_reason = _get(root, "stop_reason");
		return true;
	}
	const Array choices = _array(root, "choices");
	if (choices.size() == 0 || choices[0].get_type() != Variant::DICTIONARY) {
		r_error = "réponse inattendue (aucun choix) : " + _plain_summary(p_body);
		return false;
	}
	const Dictionary choice = choices[0].as_dictionary();
	const Dictionary message = _dict(choice, "message");
	r_message.text = strip_reasoning(_content_text(message.get(Variant("content"), Variant())));
	if (_blank(r_message.text)) {
		r_message.text.clear();
	}
	Array calls = _array(message, "tool_calls");
	if (calls.size() == 0 && message.get(Variant("function_call"), Variant()).get_type() == Variant::DICTIONARY) {
		// Ancienne forme, encore renvoyée par quelques serveurs : un seul appel.
		Dictionary item;
		item[Variant("function")] = message.get(Variant("function_call"), Variant());
		calls.push_back(Variant(item));
	}
	for (int i = 0; i < calls.size(); i++) {
		if (calls[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary item = calls[i].as_dictionary();
		// La fonction est décrite dans « function » ; quelques serveurs la mettent à plat.
		const Dictionary function = item.has(Variant("function")) ? _dict(item, "function") : item;
		ToolCall call;
		call.id = unique_call_id(_get(item, "id"), p_used_ids);
		call.name = str_strip(_get(function, "name"));
		_read_arguments(function.get(Variant("arguments"), Variant()), call);
		// Gemini : signature de réflexion, à renvoyer avec l'appel dans les requêtes suivantes.
		if (item.get(Variant("extra_content"), Variant()).get_type() == Variant::DICTIONARY) {
			call.extra = item.get(Variant("extra_content"), Variant()).as_dictionary();
		}
		if (call.name.empty()) {
			call.state = ToolCall::DONE;
			call.is_error = true;
			call.result = "Appel d'outil sans nom : rien n'a été exécuté. Rappelle l'outil en donnant son nom.";
		}
		r_message.calls.push_back(call);
	}
	r_stop_reason = _get(choice, "finish_reason");
	return true;
}

bool parse_models(const String &p_body, int p_http_status, std::vector<String> &r_names, String &r_error) {
	r_names.clear();
	r_error.clear();
	Variant parsed;
	if (!JSON::parse(p_body, parsed) || (parsed.get_type() != Variant::DICTIONARY && parsed.get_type() != Variant::ARRAY)) {
		r_error = _blank(p_body) ? String("réponse vide") : (p_http_status >= 400 ? extract_error(p_body, p_http_status) : "réponse illisible : " + _plain_summary(p_body));
		return false;
	}
	Array list;
	if (parsed.get_type() == Variant::ARRAY) {
		list = parsed.as_array();
	} else {
		const Dictionary root = parsed.as_dictionary();
		r_error = extract_error(p_body, p_http_status);
		if (!r_error.empty()) {
			return false;
		}
		list = _array(root, "data");
		if (list.size() == 0) {
			list = _array(root, "models"); // Gemini, API propre à Ollama
		}
	}
	std::set<String> names;
	std::set<String> embeddings; // modèles de plongement : inutilisables pour converser
	for (int i = 0; i < list.size(); i++) {
		String name;
		bool embedding = false;
		if (list[i].get_type() == Variant::STRING) {
			name = list[i].as_string();
		} else if (list[i].get_type() == Variant::DICTIONARY) {
			const Dictionary item = list[i].as_dictionary();
			embedding = str_begins_with(str_to_lower(_get(item, "type")), "embedding");
			for (const char *key : { "id", "name", "model", "key" }) {
				name = _get(item, key);
				if (!name.empty()) {
					break;
				}
			}
		}
		if (str_begins_with(name, "models/")) {
			name = name.substr(7);
		}
		name = str_strip(name);
		if (name.empty()) {
			continue;
		}
		// LM Studio liste aussi ses modèles de plongement (« text-embedding-... ») sous /v1/models.
		const String lower = str_to_lower(name);
		if (embedding || lower.find("text-embedding") != String::npos || lower.find("embed-text") != String::npos) {
			embeddings.insert(name);
		} else {
			names.insert(name);
		}
	}
	if (names.empty()) {
		names = embeddings; // rien d'autre : mieux vaut les montrer qu'annoncer une liste vide
	}
	if (names.empty()) {
		r_error = "le service ne propose aucun modèle";
		return false;
	}
	r_names.assign(names.begin(), names.end());
	return true;
}

// ---------------------------------------------------------------------------
// Capacités des modèles locaux

ToolSupport lmstudio_tool_support(const String &p_body, const String &p_model, int *r_context) {
	if (r_context) {
		*r_context = 0;
	}
	Variant parsed;
	if (!JSON::parse(p_body, parsed) || parsed.get_type() != Variant::DICTIONARY) {
		return TOOLS_UNKNOWN;
	}
	const Dictionary root = parsed.as_dictionary();
	// /api/v1/models : { models: [{ key, capabilities: { trained_for_tool_use }, loaded_instances }] }
	// /api/v0/models : { data: [{ id, capabilities: ["tool_use"], loaded_context_length }] }
	const bool recent = root.get(Variant("models"), Variant()).get_type() == Variant::ARRAY;
	const Array list = recent ? _array(root, "models") : _array(root, "data");
	// Un modèle chargé plusieurs fois porte un suffixe : « gemma-3-4b:2 ».
	const auto base_name = [](const String &p_name) {
		String name = str_to_lower(str_strip(p_name));
		const size_t colon = name.rfind(':');
		if (colon != String::npos && colon + 1 < name.size() && _is_digit(name[colon + 1])) {
			bool digits = true;
			for (size_t i = colon + 1; i < name.size(); i++) {
				digits = digits && _is_digit(name[i]);
			}
			if (digits) {
				name.resize(colon);
			}
		}
		return name;
	};
	const String wanted = base_name(p_model);
	bool any_capabilities = false;
	bool found = false;
	Dictionary match;
	for (int i = 0; i < list.size(); i++) {
		if (list[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary item = list[i].as_dictionary();
		any_capabilities = any_capabilities || item.has(Variant("capabilities"));
		if (found) {
			continue;
		}
		bool same = base_name(_get(item, recent ? "key" : "id")) == wanted;
		const Array instances = _array(item, "loaded_instances");
		for (int j = 0; j < instances.size() && !same; j++) {
			if (instances[j].get_type() == Variant::DICTIONARY) {
				same = base_name(_get(instances[j].as_dictionary(), "id")) == wanted;
			}
		}
		if (same) {
			found = true;
			match = item;
		}
	}
	if (!found || wanted.empty()) {
		return TOOLS_UNKNOWN;
	}
	if (r_context) {
		int context = (int)match.get(Variant("loaded_context_length"), Variant(0)).as_int();
		const Array instances = _array(match, "loaded_instances");
		for (int j = 0; j < instances.size(); j++) {
			if (instances[j].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const int length = (int)_dict(instances[j].as_dictionary(), "config").get(Variant("context_length"), Variant(0)).as_int();
			if (length > 0 && (context == 0 || length < context)) {
				context = length;
			}
		}
		*r_context = std::max(0, context);
	}
	const String type = _get(match, "type");
	if (type == "embeddings" || type == "embedding") {
		return TOOLS_NO;
	}
	const Variant capabilities = match.get(Variant("capabilities"), Variant());
	if (capabilities.get_type() == Variant::DICTIONARY) {
		const Dictionary details = capabilities.as_dictionary();
		if (!details.has(Variant("trained_for_tool_use"))) {
			return TOOLS_UNKNOWN;
		}
		return details.get(Variant("trained_for_tool_use"), Variant(false)).as_bool() ? TOOLS_YES : TOOLS_NO;
	}
	if (capabilities.get_type() == Variant::ARRAY) {
		return capabilities.as_array().has(Variant("tool_use")) ? TOOLS_YES : TOOLS_NO;
	}
	// /api/v0 n'écrit « capabilities » que pour les modèles qui en ont : son absence vaut
	// « non » si un autre modèle de la liste en déclare, sinon on ne sait pas.
	return any_capabilities ? TOOLS_NO : TOOLS_UNKNOWN;
}

ToolSupport ollama_tool_support(const String &p_body) {
	Variant parsed;
	if (!JSON::parse(p_body, parsed) || parsed.get_type() != Variant::DICTIONARY) {
		return TOOLS_UNKNOWN;
	}
	const Variant capabilities = parsed.as_dictionary().get(Variant("capabilities"), Variant());
	if (capabilities.get_type() != Variant::ARRAY) {
		return TOOLS_UNKNOWN; // erreur, ou version d'Ollama qui ne les déclare pas encore
	}
	return capabilities.as_array().has(Variant("tools")) ? TOOLS_YES : TOOLS_NO;
}

} // namespace EditorAgentProtocol
