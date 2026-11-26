#include "editor_agent.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/input/input.h"
#include "core/io/json.h"
#include "core/os/process.h"
#include "core/string/ustring.h"
#include "editor_agent_tools.h"
#include "editor_node.h"
#include "editor_ui.h"

#include <SDL.h>
#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace {

const EditorAgent::Provider PROVIDERS[] = {
	{ "anthropic", "Claude (Anthropic)", true, "https://api.anthropic.com/v1", "claude-sonnet-5", "ANTHROPIC_API_KEY", "Clé à créer sur console.anthropic.com (commence par sk-ant-)." },
	{ "openai", "ChatGPT (OpenAI)", false, "https://api.openai.com/v1", "gpt-5.6", "OPENAI_API_KEY", "Clé à créer sur platform.openai.com (commence par sk-)." },
	{ "openrouter", "OpenRouter", false, "https://openrouter.ai/api/v1", "openrouter/auto", "OPENROUTER_API_KEY", "Une seule clé pour des centaines de modèles (openrouter.ai, commence par sk-or-)." },
	{ "gemini", "Gemini (Google)", false, "https://generativelanguage.googleapis.com/v1beta/openai", "", "GEMINI_API_KEY", "Clé à créer sur aistudio.google.com ; point d'accès compatible OpenAI de Google." },
	{ "mistral", "Mistral", false, "https://api.mistral.ai/v1", "mistral-large-latest", "MISTRAL_API_KEY", "Clé à créer sur console.mistral.ai." },
	{ "deepseek", "DeepSeek", false, "https://api.deepseek.com/v1", "", "DEEPSEEK_API_KEY", "Clé à créer sur platform.deepseek.com." },
	{ "groq", "Groq", false, "https://api.groq.com/openai/v1", "", "GROQ_API_KEY", "Clé à créer sur console.groq.com." },
	{ "local", "Modèle local (Ollama, LM Studio)", false, "http://localhost:11434/v1", "", "", "Aucune clé. Ollama écoute sur le port 11434, LM Studio sur 1234 : adaptez l'adresse. Le modèle doit savoir appeler des outils." },
	{ "compatible", "Autre service compatible OpenAI", false, "", "", "MEX_AGENT_KEY", "Tout service qui offre le point d'accès /chat/completions : saisissez son adresse, sa clé et le nom du modèle." },
};
const int PROVIDER_COUNT = (int)(sizeof(PROVIDERS) / sizeof(PROVIDERS[0]));

const int MAX_ROUNDS = 40; // appels au modèle pour une seule demande de l'utilisateur
const size_t MAX_RESULT_SIZE = 24000; // caractères d'un résultat d'outil renvoyés au modèle

// Valeur entre guillemets d'un fichier de configuration de curl.
String _curl_quote(const String &p_text) {
	String out = "\"";
	for (const char c : p_text) {
		if (c == '\\' || c == '"') {
			out += '\\';
			out += c;
		} else if (c == '\n' || c == '\r') {
			out += ' '; // jamais de saut de ligne dans un en-tête
		} else {
			out += c;
		}
	}
	return out + "\"";
}

String _get(const Dictionary &p_dictionary, const char *p_key) {
	return p_dictionary.get(Variant(p_key), Variant()).as_string();
}

} // namespace

struct EditorAgent::Job {
	std::thread thread;
	std::atomic<bool> done{ false };
	std::atomic<bool> cancel{ false };
	bool models = false;
	bool started = false;
	int exit_code = 0;
	String output;
	String errors;
};

int EditorAgent::get_provider_count() {
	return PROVIDER_COUNT;
}

const EditorAgent::Provider &EditorAgent::get_provider(int p_index) {
	return PROVIDERS[std::clamp(p_index, 0, PROVIDER_COUNT - 1)];
}

EditorAgent::EditorAgent(EditorNode *p_editor) :
		editor(p_editor) {
}

EditorAgent::~EditorAgent() {
	if (job) {
		job->cancel = true;
		if (job->thread.joinable()) {
			job->thread.join();
		}
	}
}

// ---------------------------------------------------------------------------
// Réglages : dans le dossier de préférences de l'utilisateur, jamais dans le projet (les clés
// ne doivent pas partir dans un dépôt Git ni dans un jeu exporté).

void EditorAgent::load_settings() {
	const String path = editor->prefs_dir + "agent.cfg";
	ConfigFile config;
	if (!editor->prefs_dir.empty() && FileAccess::exists(path) && config.load(path) == OK) {
		const String id = config.get_value("agent", "fournisseur", Variant(String(PROVIDERS[0].id))).as_string();
		for (int i = 0; i < PROVIDER_COUNT; i++) {
			if (id == PROVIDERS[i].id) {
				provider_index = i;
			}
			ProviderSettings &settings = provider_settings[PROVIDERS[i].id];
			settings.key = config.get_value(PROVIDERS[i].id, "cle", Variant(String())).as_string();
			settings.model = config.get_value(PROVIDERS[i].id, "modele", Variant(String())).as_string();
			settings.url = config.get_value(PROVIDERS[i].id, "adresse", Variant(String())).as_string();
		}
		max_tokens = (int)config.get_value("agent", "jetons_par_reponse", Variant(max_tokens)).as_int();
		animations = config.get_value("agent", "animations", Variant(animations)).as_bool();
		agent_mode = config.get_value("agent", "mode_agent", Variant(false)).as_bool();
		ask_before_changes = config.get_value("agent", "demander_avant_de_modifier", Variant(ask_before_changes)).as_bool();
		open = config.get_value("agent", "panneau_visible", Variant(open)).as_bool();
	}
	// Variables d'environnement : tests automatiques et installations partagées.
	if (const char *forced = SDL_getenv("MEX_AGENT_PROVIDER")) {
		for (int i = 0; i < PROVIDER_COUNT; i++) {
			if (SDL_strcmp(forced, PROVIDERS[i].id) == 0) {
				provider_index = i;
			}
		}
	}
	if (const char *forced = SDL_getenv("MEX_AGENT_URL")) {
		_settings().url = forced;
	}
	if (const char *forced = SDL_getenv("MEX_AGENT_MODEL")) {
		_settings().model = forced;
	}
	if (const char *forced = SDL_getenv("MEX_AGENT_ANIMATIONS")) {
		animations_forced_off = forced[0] == '0';
	}
	if (const char *forced = SDL_getenv("MEX_AGENT_AUTO")) {
		ask_before_changes = SDL_strcmp(forced, "1") != 0;
	}
	max_tokens = std::clamp(max_tokens, 256, 128000);
}

void EditorAgent::save_settings() const {
	if (editor->prefs_dir.empty()) {
		return;
	}
	ConfigFile config;
	config.set_value("agent", "fournisseur", Variant(String(PROVIDERS[provider_index].id)));
	config.set_value("agent", "jetons_par_reponse", Variant(max_tokens));
	config.set_value("agent", "animations", Variant(animations));
	config.set_value("agent", "mode_agent", Variant(agent_mode));
	config.set_value("agent", "demander_avant_de_modifier", Variant(ask_before_changes));
	config.set_value("agent", "panneau_visible", Variant(open));
	for (int i = 0; i < PROVIDER_COUNT; i++) {
		const auto found = provider_settings.find(PROVIDERS[i].id);
		if (found == provider_settings.end()) {
			continue;
		}
		if (!found->second.key.empty()) {
			config.set_value(PROVIDERS[i].id, "cle", Variant(found->second.key));
		}
		if (!found->second.model.empty()) {
			config.set_value(PROVIDERS[i].id, "modele", Variant(found->second.model));
		}
		if (!found->second.url.empty()) {
			config.set_value(PROVIDERS[i].id, "adresse", Variant(found->second.url));
		}
	}
	if (config.save(editor->prefs_dir + "agent.cfg") != OK) {
		print_warning("Impossible d'enregistrer les réglages de MEX Agent.");
	}
}

EditorAgent::ProviderSettings &EditorAgent::_settings() {
	return provider_settings[PROVIDERS[provider_index].id];
}

String EditorAgent::_effective_key(const Provider &p_provider, const ProviderSettings &p_settings) const {
	if (!p_settings.key.empty()) {
		return str_strip(p_settings.key);
	}
	if (p_provider.key_variable[0]) {
		if (const char *value = SDL_getenv(p_provider.key_variable)) {
			return str_strip(String(value));
		}
	}
	return String();
}

String EditorAgent::_effective_url(const Provider &p_provider, const ProviderSettings &p_settings) const {
	String url = str_strip(p_settings.url.empty() ? String(p_provider.default_url) : p_settings.url);
	while (!url.empty() && url.back() == '/') {
		url.pop_back();
	}
	// Adresse complète collée par l'utilisateur : on garde la base.
	for (const char *suffix : { "/chat/completions", "/messages" }) {
		if (str_ends_with(url, suffix)) {
			url = url.substr(0, url.size() - SDL_strlen(suffix));
		}
	}
	return url;
}

String EditorAgent::_effective_model(const Provider &p_provider, const ProviderSettings &p_settings) const {
	return str_strip(p_settings.model.empty() ? String(p_provider.default_model) : p_settings.model);
}

// ---------------------------------------------------------------------------
// Compétences : fiches de savoir-faire que le modèle charge à la demande.

void EditorAgent::_load_skills() {
	const String project = ProjectSettings::get_singleton()->get_resource_path();
	if (skills_loaded && skills_project == project) {
		return;
	}
	skills_loaded = true;
	skills_project = project;
	skills.clear();
	const auto add = [this](const String &p_text, const String &p_origin, const String &p_fallback_name) {
		Skill skill;
		skill.origin = p_origin;
		skill.name = p_fallback_name;
		skill.body = p_text;
		// En-tête facultatif : --- / nom: ... / description: ... / ---
		if (str_begins_with(p_text, "---")) {
			const size_t end = p_text.find("\n---", 3);
			if (end != String::npos) {
				for (const String &line : str_split(p_text.substr(3, end - 3), "\n", false)) {
					const size_t colon = line.find(':');
					if (colon == String::npos) {
						continue;
					}
					const String key = str_strip(line.substr(0, colon));
					const String value = str_strip(line.substr(colon + 1));
					if (key == "nom" || key == "name") {
						skill.name = value;
					} else if (key == "description") {
						skill.description = value;
					}
				}
				const size_t body = p_text.find('\n', end + 1);
				skill.body = body == String::npos ? String() : str_strip(p_text.substr(body + 1));
			}
		}
		if (skill.name.empty() || skill.body.empty()) {
			return;
		}
		if (skill.description.empty()) {
			skill.description = "Compétence « " + skill.name + " ».";
		}
		// Une fiche du projet ou de l'utilisateur remplace celle de MEX qui porte le même nom.
		for (Skill &existing : skills) {
			if (existing.name == skill.name) {
				existing = skill;
				return;
			}
		}
		skills.push_back(skill);
	};
	for (int i = 0; i < EditorAgentTools::get_builtin_skill_count(); i++) {
		add(EditorAgentTools::get_builtin_skill(i), "MEX", String());
	}
	const auto add_folder = [&](const String &p_folder, const String &p_origin) {
		if (p_folder.empty() || !FileAccess::dir_exists(p_folder)) {
			return;
		}
		std::vector<String> files, dirs;
		FileAccess::list_dir(p_folder, &files, &dirs);
		std::sort(files.begin(), files.end());
		for (const String &file : files) {
			if (!str_ends_with(file, ".md")) {
				continue;
			}
			Error err = OK;
			const String text = FileAccess::get_file_as_string(p_folder + "/" + file, &err);
			if (err == OK) {
				add(text, p_origin, file.substr(0, file.size() - 3));
			}
		}
	};
	add_folder(editor->prefs_dir + "competences", "utilisateur");
	if (!project.empty()) {
		add_folder(project + "/competences", "projet");
	}
}

const std::vector<EditorAgent::Skill> &EditorAgent::get_skills() {
	_load_skills();
	return skills;
}

String EditorAgent::_system_prompt() const {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	String text =
			"Tu es MEX Agent, l'assistant de création de jeux intégré à l'éditeur MEX. MEX est un moteur de jeu 2D et 3D "
			"avec son propre langage de script, MexScript (fichiers .mx), ses scènes en texte (.scn) et un fichier de "
			"projet (project.mex). Tu aides l'utilisateur à créer son jeu en agissant directement sur le projet ouvert, "
			"avec tes outils.\n\n"
			"# Règles\n"
			"- MEX ressemble à Godot mais n'est PAS Godot : beaucoup de classes, de méthodes et de constructions de "
			"GDScript n'existent pas. Ne suppose rien : `documentation_classe` donne les propriétés, méthodes et "
			"signaux réels d'une classe, et les compétences ci-dessous donnent la syntaxe exacte.\n"
			"- Avant d'écrire un script ou une scène, charge la compétence correspondante avec `charger_competence` "
			"(une fois par conversation suffit).\n"
			"- Regarde avant d'agir : `lister_fichiers` et `lire_fichier` pour connaître le projet. Relis un fichier "
			"avant de le modifier ; l'utilisateur a pu le changer.\n"
			"- Avance par petites étapes vérifiées. Après un changement de comportement, `lancer_jeu` et lis la sortie. "
			"Une erreur dans la sortie se corrige avant de continuer.\n"
			"- `lancer_jeu` renvoie du texte : tu ne vois pas l'image et tu ne joues pas. Ne prétends pas avoir vu ou "
			"essayé ce que tu n'as pas pu observer ; dis à l'utilisateur ce qu'il doit essayer lui-même.\n"
			"- Reste dans la demande. N'efface et ne réécris pas le travail existant sans raison ; préfère "
			"`remplacer_dans_fichier` pour un petit changement.\n"
			"- Les chemins commencent par res:// (la racine du projet). Tu ne peux rien lire ni écrire ailleurs.\n"
			"- Réponds dans la langue de l'utilisateur, simplement et brièvement. Pas de long code dans la réponse "
			"quand il est déjà écrit dans un fichier. Termine en disant ce qui a été fait, ce qui a été vérifié, et "
			"ce qui reste à essayer.\n\n";
	text += "# Projet ouvert\n";
	text += "- Nom : " + ps->get_setting("application/name").as_string() + "\n";
	const String main_scene = ps->get_setting("application/main_scene").as_string();
	text += "- Scène principale : " + (main_scene.empty() ? String("(aucune pour l'instant)") : main_scene) + "\n";
	text += "- Fenêtre du jeu : " + ps->get_setting("display/width").as_string() + " x " + ps->get_setting("display/height").as_string() + " pixels\n";
	text += "- Scène ouverte dans l'éditeur : " + (editor->scene_path.empty() ? String("(aucune ou non enregistrée)") : editor->scene_path) + "\n\n";
	text += "# Compétences disponibles (outil `charger_competence`)\n";
	for (const Skill &skill : skills) {
		text += "- `" + skill.name + "` : " + skill.description + "\n";
	}
	return text;
}

// ---------------------------------------------------------------------------
// Requêtes

Dictionary EditorAgent::build_request(const Provider &p_provider, const ProviderSettings &p_settings) const {
	Dictionary request;
	request[Variant("model")] = Variant(_effective_model(p_provider, p_settings));
	Array tools;
	const Array definitions = EditorAgentTools::get_definitions();
	Array list;
	const String system = _system_prompt();

	if (p_provider.anthropic_format) {
		// API « Messages » d'Anthropic : le texte et les appels d'outils sont des blocs de
		// contenu ; les résultats reviennent dans un message de l'utilisateur.
		request[Variant("max_tokens")] = Variant(max_tokens);
		request[Variant("system")] = Variant(system);
		for (int i = 0; i < definitions.size(); i++) {
			const Dictionary definition = definitions[i].as_dictionary();
			Dictionary tool;
			tool[Variant("name")] = definition.get(Variant("name"), Variant());
			tool[Variant("description")] = definition.get(Variant("description"), Variant());
			tool[Variant("input_schema")] = definition.get(Variant("parameters"), Variant());
			tools.push_back(Variant(tool));
		}
		for (const Message &message : messages) {
			if (message.kind == Message::NOTICE) {
				continue;
			}
			Dictionary entry;
			if (message.kind == Message::USER) {
				entry[Variant("role")] = Variant("user");
				entry[Variant("content")] = Variant(message.text);
				list.push_back(Variant(entry));
				continue;
			}
			Array content;
			if (!message.text.empty()) {
				Dictionary block;
				block[Variant("type")] = Variant("text");
				block[Variant("text")] = Variant(message.text);
				content.push_back(Variant(block));
			}
			Array results;
			for (const ToolCall &call : message.calls) {
				Dictionary block;
				block[Variant("type")] = Variant("tool_use");
				block[Variant("id")] = Variant(call.id);
				block[Variant("name")] = Variant(call.name);
				block[Variant("input")] = Variant(call.arguments);
				content.push_back(Variant(block));
				Dictionary result;
				result[Variant("type")] = Variant("tool_result");
				result[Variant("tool_use_id")] = Variant(call.id);
				result[Variant("content")] = Variant(call.result.empty() ? String("(aucun résultat)") : call.result);
				if (call.is_error) {
					result[Variant("is_error")] = Variant(true);
				}
				results.push_back(Variant(result));
			}
			if (content.size() == 0) {
				continue; // réponse vide : rien à renvoyer
			}
			entry[Variant("role")] = Variant("assistant");
			entry[Variant("content")] = Variant(content);
			list.push_back(Variant(entry));
			if (results.size() > 0) {
				Dictionary reply;
				reply[Variant("role")] = Variant("user");
				reply[Variant("content")] = Variant(results);
				list.push_back(Variant(reply));
			}
		}
	} else {
		// Format « chat completions » d'OpenAI, repris par OpenRouter, Mistral, Groq, Ollama...
		for (int i = 0; i < definitions.size(); i++) {
			Dictionary tool;
			tool[Variant("type")] = Variant("function");
			tool[Variant("function")] = definitions[i];
			tools.push_back(Variant(tool));
		}
		Dictionary first;
		first[Variant("role")] = Variant("system");
		first[Variant("content")] = Variant(system);
		list.push_back(Variant(first));
		for (const Message &message : messages) {
			if (message.kind == Message::NOTICE) {
				continue;
			}
			Dictionary entry;
			if (message.kind == Message::USER) {
				entry[Variant("role")] = Variant("user");
				entry[Variant("content")] = Variant(message.text);
				list.push_back(Variant(entry));
				continue;
			}
			if (message.text.empty() && message.calls.empty()) {
				continue;
			}
			entry[Variant("role")] = Variant("assistant");
			entry[Variant("content")] = message.text.empty() ? Variant() : Variant(message.text);
			if (!message.calls.empty()) {
				Array calls;
				for (const ToolCall &call : message.calls) {
					Dictionary function;
					function[Variant("name")] = Variant(call.name);
					function[Variant("arguments")] = Variant(JSON::stringify(Variant(call.arguments)));
					Dictionary item;
					item[Variant("id")] = Variant(call.id);
					item[Variant("type")] = Variant("function");
					item[Variant("function")] = Variant(function);
					calls.push_back(Variant(item));
				}
				entry[Variant("tool_calls")] = Variant(calls);
			}
			list.push_back(Variant(entry));
			for (const ToolCall &call : message.calls) {
				Dictionary result;
				result[Variant("role")] = Variant("tool");
				result[Variant("tool_call_id")] = Variant(call.id);
				result[Variant("content")] = Variant(call.result.empty() ? String("(aucun résultat)") : (call.is_error ? "ERREUR : " + call.result : call.result));
				list.push_back(Variant(result));
			}
		}
	}
	request[Variant("tools")] = Variant(tools);
	request[Variant("messages")] = Variant(list);
	return request;
}

bool EditorAgent::parse_response(const Provider &p_provider, const String &p_body, Message &r_message, String &r_error, String &r_stop_reason) const {
	Variant parsed;
	String json_error;
	if (!JSON::parse(p_body, parsed, &json_error) || parsed.get_type() != Variant::DICTIONARY) {
		r_error = "Réponse illisible du service (" + json_error + ") : " + p_body.substr(0, 300);
		return false;
	}
	const Dictionary root = parsed.as_dictionary();
	if (root.has(Variant("error")) && root.get(Variant("error"), Variant()).get_type() != Variant::NIL) {
		const Variant error = root.get(Variant("error"), Variant());
		r_error = error.get_type() == Variant::DICTIONARY ? _get(error.as_dictionary(), "message") : error.as_string();
		if (r_error.empty()) {
			r_error = JSON::stringify(error);
		}
		return false;
	}
	r_message.kind = Message::ASSISTANT;
	if (p_provider.anthropic_format) {
		const Array content = root.get(Variant("content"), Variant(Array())).as_array();
		for (int i = 0; i < content.size(); i++) {
			const Dictionary block = content[i].as_dictionary();
			const String type = _get(block, "type");
			if (type == "text") {
				r_message.text += _get(block, "text");
			} else if (type == "tool_use") {
				ToolCall call;
				call.id = _get(block, "id");
				call.name = _get(block, "name");
				call.arguments = block.get(Variant("input"), Variant(Dictionary())).as_dictionary();
				r_message.calls.push_back(call);
			}
		}
		r_stop_reason = _get(root, "stop_reason");
		if (!root.has(Variant("content"))) {
			r_error = "Réponse inattendue du service : " + p_body.substr(0, 300);
			return false;
		}
		return true;
	}
	const Array choices = root.get(Variant("choices"), Variant(Array())).as_array();
	if (choices.size() == 0) {
		r_error = "Réponse inattendue du service : " + p_body.substr(0, 300);
		return false;
	}
	const Dictionary choice = choices[0].as_dictionary();
	const Dictionary message = choice.get(Variant("message"), Variant(Dictionary())).as_dictionary();
	const Variant content = message.get(Variant("content"), Variant());
	if (content.get_type() == Variant::STRING) {
		r_message.text = content.as_string();
	} else if (content.get_type() == Variant::ARRAY) {
		// Certains services renvoient le texte en morceaux.
		const Array parts = content.as_array();
		for (int i = 0; i < parts.size(); i++) {
			if (parts[i].get_type() == Variant::DICTIONARY) {
				r_message.text += _get(parts[i].as_dictionary(), "text");
			}
		}
	}
	const Array calls = message.get(Variant("tool_calls"), Variant(Array())).as_array();
	for (int i = 0; i < calls.size(); i++) {
		const Dictionary item = calls[i].as_dictionary();
		const Dictionary function = item.get(Variant("function"), Variant(Dictionary())).as_dictionary();
		ToolCall call;
		call.id = _get(item, "id");
		if (call.id.empty()) {
			call.id = "appel_" + std::to_string(messages.size()) + "_" + std::to_string(i);
		}
		call.name = _get(function, "name");
		const Variant arguments = function.get(Variant("arguments"), Variant());
		if (arguments.get_type() == Variant::DICTIONARY) {
			call.arguments = arguments.as_dictionary();
		} else {
			const String text = arguments.as_string();
			Variant value;
			String error;
			if (str_strip(text).empty()) {
				// aucun argument
			} else if (JSON::parse(text, value, &error) && value.get_type() == Variant::DICTIONARY) {
				call.arguments = value.as_dictionary();
			} else {
				// Le modèle saura corriger : l'erreur lui revient comme résultat de l'outil.
				call.state = ToolCall::DONE;
				call.is_error = true;
				call.result = "Arguments illisibles (JSON invalide : " + error + "). Rappelle l'outil avec un objet JSON valide.";
			}
		}
		r_message.calls.push_back(call);
	}
	r_stop_reason = _get(choice, "finish_reason");
	return true;
}

void EditorAgent::_notice(const String &p_text) {
	Message message;
	message.kind = Message::NOTICE;
	message.text = p_text;
	messages.push_back(message);
	scroll_to_end = 3;
}

void EditorAgent::submit(const String &p_text) {
	const String text = str_strip(p_text);
	if (text.empty() || state != IDLE) {
		return;
	}
	// Le modèle doit lire ce que l'utilisateur voit : on enregistre les changements en cours.
	if (editor->script_dirty) {
		editor->_save_script();
	}
	if (editor->scene_root && editor->scene_modified && !editor->scene_path.empty()) {
		editor->_save_scene(editor->scene_path);
	}
	Message message;
	message.kind = Message::USER;
	message.text = text;
	messages.push_back(message);
	scroll_to_end = 3;
	rounds = 0;
	_start_request();
}

void EditorAgent::_start_request() {
	_load_skills();
	const Provider &provider = PROVIDERS[provider_index];
	const ProviderSettings &settings = _settings();
	const String url = _effective_url(provider, settings);
	if (url.empty()) {
		_notice("Aucune adresse de service. Ouvrez « Connexion » et saisissez l'adresse du service.");
		state = IDLE;
		return;
	}
	if (_effective_model(provider, settings).empty()) {
		_notice("Aucun modèle choisi. Ouvrez « Connexion » : saisissez le nom du modèle ou utilisez « Lister ».");
		state = IDLE;
		return;
	}
	if (_effective_key(provider, settings).empty() && provider.key_variable[0] && String(provider.id) != "compatible") {
		_notice(String("Aucune clé d'API pour ") + provider.label + ". Ouvrez « Connexion » et collez votre clé. " + provider.help);
		show_settings = true;
		state = IDLE;
		return;
	}
	if (++rounds > MAX_ROUNDS) {
		_notice("MEX Agent s'est arrêté après " + std::to_string(MAX_ROUNDS) + " étapes pour cette demande. Écrivez « continue » pour qu'il reprenne.");
		state = IDLE;
		return;
	}
	const String body = JSON::stringify(Variant(build_request(provider, settings)));
	state = REQUESTING;
	status = "Le modèle réfléchit...";
	_start_job(url + (provider.anthropic_format ? "/messages" : "/chat/completions"), body, false);
}

void EditorAgent::_start_job(const String &p_url, const String &p_body, bool p_models) {
	const Provider &provider = PROVIDERS[provider_index];
	const String key = _effective_key(provider, _settings());
	// La clé voyage sur l'entrée standard de curl : elle n'apparaît ni dans la liste des
	// processus ni dans un fichier. Le corps de la requête passe par un fichier temporaire du
	// dossier de préférences, désigné par un nom relatif (aucun souci d'accents dans le chemin).
	const String directory = editor->prefs_dir;
	const String body_name = "agent_requete.json";
	String config = "url = " + _curl_quote(p_url) + "\nsilent\nshow-error\nlocation\nconnect-timeout = 30\nmax-time = 900\n";
	config += "write-out = \"\\n%{http_code}\"\n";
	config += "header = \"content-type: application/json\"\n";
	if (provider.anthropic_format) {
		config += "header = \"anthropic-version: 2023-06-01\"\n";
		if (!key.empty()) {
			config += "header = " + _curl_quote("x-api-key: " + key) + "\n";
		}
	} else {
		if (!key.empty()) {
			config += "header = " + _curl_quote("Authorization: Bearer " + key) + "\n";
		}
		if (String(provider.id) == "openrouter") {
			config += "header = \"X-Title: MEX\"\n";
		}
	}
	if (!p_models) {
		config += "request = \"POST\"\ndata-binary = \"@" + body_name + "\"\n";
		if (FileAccess::write_string(directory + body_name, p_body) != OK) {
			_notice("Impossible d'écrire la requête dans " + directory);
			state = IDLE;
			return;
		}
	}
	job = std::make_unique<Job>();
	job->models = p_models;
	Job *current = job.get();
	current->thread = std::thread([current, config, directory, body_name, p_models]() {
		current->started = Process::run("curl", { "--config", "-" }, config, &current->output, &current->errors, &current->exit_code, &current->cancel, directory);
		if (!p_models) {
			FileAccess::remove(directory + body_name);
		}
		current->done = true;
	});
}

void EditorAgent::stop() {
	if (job) {
		job->cancel = true;
	}
	if (state == TOOLS) {
		// Les outils non exécutés reçoivent un résultat : l'historique reste cohérent.
		if (!messages.empty()) {
			for (ToolCall &call : messages.back().calls) {
				if (call.state != ToolCall::DONE) {
					if (call.state == ToolCall::RUNNING && run_started_game && editor->game.is_running()) {
						editor->_stop();
					}
					_finish_call(call, "Interrompu par l'utilisateur avant l'exécution.", true);
				}
			}
		}
		state = IDLE;
		_notice("Interrompu.");
	}
	status.clear();
}

void EditorAgent::new_conversation() {
	stop();
	if (job && job->thread.joinable()) {
		job->thread.join();
	}
	job.reset();
	listing_models = false;
	state = IDLE;
	messages.clear();
	originals.clear();
	rounds = 0;
	skills_loaded = false;
}

void EditorAgent::_poll_job() {
	if (!job || !job->done) {
		return;
	}
	if (job->thread.joinable()) {
		job->thread.join();
	}
	const std::unique_ptr<Job> finished = std::move(job);
	if (finished->models) {
		listing_models = false;
		_handle_models(finished->output, finished->exit_code, finished->started);
		return;
	}
	if (finished->cancel) {
		if (state == REQUESTING) {
			state = IDLE;
			_notice("Interrompu.");
		}
		return;
	}
	_handle_response(finished->output, finished->errors, finished->exit_code, finished->started);
}

namespace {

// Sépare le corps de la réponse du code HTTP ajouté par curl sur la dernière ligne.
int _split_status(String &r_body) {
	const size_t newline = r_body.rfind('\n');
	if (newline == String::npos) {
		return 0;
	}
	const int code = SDL_atoi(r_body.c_str() + newline + 1);
	r_body.resize(newline);
	return code;
}

String _curl_failure(bool p_started, int p_exit_code, const String &p_errors) {
	if (!p_started) {
		return "Le programme « curl » est introuvable. MEX Agent s'en sert pour joindre le service. Il est fourni avec Windows 10 (version 1803) et suivants et avec macOS ; sous Linux, installez le paquet « curl ».";
	}
	String reason = str_strip(p_errors);
	if (reason.empty()) {
		reason = "code " + std::to_string(p_exit_code);
	}
	return "Connexion impossible (" + reason + "). Vérifiez votre connexion Internet et l'adresse du service.";
}

} // namespace

void EditorAgent::_handle_response(const String &p_output, const String &p_errors, int p_exit_code, bool p_started) {
	status.clear();
	if (!p_started || p_exit_code != 0) {
		_notice(_curl_failure(p_started, p_exit_code, p_errors));
		state = IDLE;
		return;
	}
	String body = p_output;
	const int http = _split_status(body);
	const Provider &provider = PROVIDERS[provider_index];
	Message message;
	String error, stop_reason;
	if (!parse_response(provider, body, message, error, stop_reason)) {
		String text = "Le service a refusé la requête";
		if (http != 0 && http != 200) {
			text += " (HTTP " + std::to_string(http) + ")";
		}
		text += " : " + error;
		if (http == 401 || http == 403) {
			text += "\nLa clé d'API est absente, erronée ou sans droit sur ce modèle.";
		} else if (http == 404) {
			text += "\nLe modèle ou l'adresse n'existe pas : « Lister » montre les modèles de votre compte.";
		} else if (http == 429) {
			text += "\nLimite de débit ou crédit épuisé chez le fournisseur.";
		}
		_notice(text);
		state = IDLE;
		return;
	}
	const bool truncated = stop_reason == "max_tokens" || stop_reason == "length";
	if (truncated && !message.calls.empty()) {
		// Un appel d'outil coupé en plein milieu a des arguments incomplets.
		ToolCall &last = message.calls.back();
		if (last.state != ToolCall::DONE) {
			last.state = ToolCall::DONE;
			last.is_error = true;
			last.result = "La réponse a été coupée (limite de jetons) avant la fin de cet appel : rien n'a été exécuté. Refais-le avec un contenu plus court, par exemple en plusieurs fichiers ou avec remplacer_dans_fichier.";
		}
	}
	messages.push_back(message);
	scroll_to_end = 3;
	if (messages.back().calls.empty()) {
		if (truncated) {
			_notice("La réponse a été coupée par la limite de jetons (" + std::to_string(max_tokens) + "). Augmentez « Jetons par réponse » dans « Connexion » ou écrivez « continue ».");
		} else if (messages.back().text.empty()) {
			_notice("Le modèle a renvoyé une réponse vide.");
		}
		state = IDLE;
		return;
	}
	state = TOOLS;
}

void EditorAgent::_handle_models(const String &p_output, int p_exit_code, bool p_started) {
	if (!p_started || p_exit_code != 0) {
		_notice(_curl_failure(p_started, p_exit_code, String()));
		return;
	}
	String body = p_output;
	const int http = _split_status(body);
	Variant parsed;
	std::vector<String> names;
	if (JSON::parse(body, parsed) && parsed.get_type() == Variant::DICTIONARY) {
		const Dictionary root = parsed.as_dictionary();
		Variant list = root.get(Variant("data"), Variant());
		if (list.get_type() != Variant::ARRAY) {
			list = root.get(Variant("models"), Variant());
		}
		if (list.get_type() == Variant::ARRAY) {
			const Array array = list.as_array();
			for (int i = 0; i < array.size(); i++) {
				if (array[i].get_type() == Variant::DICTIONARY) {
					String name = _get(array[i].as_dictionary(), "id");
					if (name.empty()) {
						name = _get(array[i].as_dictionary(), "name");
					}
					if (str_begins_with(name, "models/")) {
						name = name.substr(7);
					}
					if (!name.empty()) {
						names.push_back(name);
					}
				}
			}
		}
	}
	if (names.empty()) {
		_notice("Impossible de lister les modèles (HTTP " + std::to_string(http) + ") : " + body.substr(0, 300));
		return;
	}
	std::sort(names.begin(), names.end());
	_settings().models = names;
}

void EditorAgent::_finish_call(ToolCall &p_call, const String &p_result, bool p_is_error) {
	p_call.result = p_result;
	if (p_call.result.size() > MAX_RESULT_SIZE) {
		p_call.result.resize(MAX_RESULT_SIZE);
		// Pas de coupure au milieu d'un caractère UTF-8.
		while (!p_call.result.empty() && ((unsigned char)p_call.result.back() & 0xC0) == 0x80) {
			p_call.result.pop_back();
		}
		if (!p_call.result.empty() && ((unsigned char)p_call.result.back() & 0x80)) {
			p_call.result.pop_back();
		}
		p_call.result += "\n[... résultat tronqué : " + std::to_string(p_result.size()) + " caractères au total]";
	}
	p_call.is_error = p_is_error;
	p_call.state = ToolCall::DONE;
}

void EditorAgent::_process_tools() {
	if (state != TOOLS || messages.empty()) {
		return;
	}
	// L'indice et non une référence : un outil peut ajouter un message (avis).
	const size_t index = messages.size() - 1;
	for (size_t i = 0; i < messages[index].calls.size(); i++) {
		ToolCall &call = messages[index].calls[i];
		if (call.state == ToolCall::DONE) {
			continue;
		}
		if (call.state == ToolCall::WAITING) {
			if (ask_before_changes && EditorAgentTools::modifies_project(call.name)) {
				call.state = ToolCall::NEEDS_APPROVAL;
				scroll_to_end = 3;
				return;
			}
			call.state = ToolCall::RUNNING;
		}
		if (call.state == ToolCall::NEEDS_APPROVAL) {
			return; // les boutons « Autoriser » / « Refuser » décident
		}
		// RUNNING : un outil répond tout de suite, sauf « lancer_jeu » qui dure.
		String result;
		bool is_error = false;
		if (!EditorAgentTools::execute(this, call, result, is_error)) {
			status = "Le jeu tourne...";
			return; // encore en cours : on revient à l'image suivante
		}
		status.clear();
		_finish_call(messages[index].calls[i], result, is_error);
		scroll_to_end = 3;
		return; // un outil par image : l'interface reste fluide
	}
	_start_request();
}

void EditorAgent::_revert_changes() {
	namespace fs = std::filesystem;
	ProjectSettings *ps = ProjectSettings::get_singleton();
	const String project_file = String("res://") + ProjectSettings::PROJECT_FILE;
	const fs::path project_root = fs::u8path(ps->get_resource_path());
	int restored = 0;
	bool settings_restored = false;
	for (const auto &entry : originals) {
		if (entry.second.first) {
			if (FileAccess::write_string(entry.first, entry.second.second) == OK) {
				restored++;
				settings_restored = settings_restored || entry.first == project_file;
			}
		} else if (FileAccess::remove(entry.first) == OK) {
			restored++;
			// Dossiers créés pour ce fichier : retirés s'ils sont restés vides.
			std::error_code ec;
			fs::path dir = fs::u8path(ps->globalize_path(entry.first)).parent_path();
			while (!dir.empty() && dir != project_root && fs::is_directory(dir, ec) && fs::is_empty(dir, ec) && !ec) {
				if (!fs::remove(dir, ec) || ec) {
					break;
				}
				dir = dir.parent_path();
			}
		}
	}
	originals.clear();
	if (settings_restored) {
		// project.mex est relu : l'éditeur et les actions d'entrée retrouvent leurs réglages.
		ps->setup(ps->get_resource_path());
		editor->_apply_project_settings();
		InputMap::get_singleton()->load_from_project_settings();
	}
	EditorAgentTools::refresh_editor(this, String());
	_notice(std::to_string(restored) + " fichier(s) remis dans l'état d'avant la conversation" + (settings_restored ? ", paramètres du projet compris." : "."));
}

// ---------------------------------------------------------------------------
// Interface

void EditorAgent::draw_connection_settings() {
	const Provider &provider = PROVIDERS[provider_index];
	bool changed = false;
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##fournisseur", provider.label)) {
		for (int i = 0; i < PROVIDER_COUNT; i++) {
			if (ImGui::Selectable(PROVIDERS[i].label, i == provider_index)) {
				provider_index = i;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	ProviderSettings &settings = _settings();
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("%s", PROVIDERS[provider_index].help);
	ImGui::PopTextWrapPos();

	const float label_width = ES(118);
	if (PROVIDERS[provider_index].key_variable[0]) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Clé d'API");
		ImGui::SameLine(label_width);
		ImGui::SetNextItemWidth(-ES(34));
		const bool from_environment = settings.key.empty() && SDL_getenv(PROVIDERS[provider_index].key_variable);
		const String hint = from_environment ? String("lue dans ") + PROVIDERS[provider_index].key_variable : String("collez votre clé ici");
		changed |= ImGui::InputTextWithHint("##cle", hint.c_str(), &settings.key, show_key ? 0 : ImGuiInputTextFlags_Password);
		ImGui::SameLine();
		if (ImGui::Button(show_key ? ICON_EYE_OFF "##voir" : ICON_EYE "##voir")) {
			show_key = !show_key;
		}
		ImGui::SetItemTooltip("Afficher ou masquer la clé");
	}
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Modèle");
	ImGui::SameLine(label_width);
	ImGui::SetNextItemWidth(-ES(84));
	changed |= ImGui::InputTextWithHint("##modele", PROVIDERS[provider_index].default_model[0] ? PROVIDERS[provider_index].default_model : "nom du modèle", &settings.model);
	ImGui::SameLine();
	ImGui::BeginDisabled(listing_models || state == REQUESTING);
	if (ImGui::Button(listing_models ? "..." : "Lister", ImVec2(-FLT_MIN, 0))) {
		const String url = _effective_url(PROVIDERS[provider_index], settings);
		if (!url.empty()) {
			listing_models = true;
			_start_job(url + "/models", String(), true);
		}
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Demander au service la liste des modèles de votre compte");
	if (!settings.models.empty()) {
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_width - ImGui::GetStyle().WindowPadding.x);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##modeles", (std::to_string(settings.models.size()) + " modèles disponibles").c_str())) {
			for (const String &name : settings.models) {
				if (ImGui::Selectable(name.c_str(), name == settings.model)) {
					settings.model = name;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
	}
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Adresse");
	ImGui::SameLine(label_width);
	ImGui::SetNextItemWidth(-FLT_MIN);
	changed |= ImGui::InputTextWithHint("##adresse", PROVIDERS[provider_index].default_url[0] ? PROVIDERS[provider_index].default_url : "https://exemple.com/v1", &settings.url);
	ImGui::SetItemTooltip("Laisser vide pour l'adresse officielle. À changer pour un relais ou un serveur local.");
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Jetons max.");
	ImGui::SameLine(label_width);
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputInt("##jetons", &max_tokens, 1024, 4096)) {
		max_tokens = std::clamp(max_tokens, 256, 128000);
		changed = true;
	}
	ImGui::SetItemTooltip("Longueur maximale d'une réponse du modèle (Claude). Trop bas, les longs fichiers sont coupés.");
	changed |= ImGui::Checkbox("Demander avant de modifier", &ask_before_changes);
	ImGui::SetItemTooltip("Coché : chaque écriture de fichier et chaque réglage du projet attend votre accord.\nDécoché : MEX Agent écrit sans attendre.\nLe bouton « Annuler les changements » reste disponible.");
	if (changed) {
		save_settings();
	}
}
