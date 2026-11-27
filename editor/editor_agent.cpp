#include "editor_agent.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/input/input.h"
#include "core/io/json.h"
#include "core/os/process.h"
#include "core/string/ustring.h"
#include "editor_agent_protocol.h"
#include "editor_agent_tools.h"
#include "editor_node.h"
#include "editor_ui.h"

#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <set>

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
	{ "local", "Modèle local (Ollama, LM Studio)", false, "http://localhost:11434/v1", "", "", "Aucune clé. Ollama : http://localhost:11434 ; LM Studio : http://localhost:1234, serveur démarré (« /v1 » s'ajoute tout seul). Un modèle sans appel d'outils (Gemma 3...) discute avec vous mais n'agit pas sur le projet ; pour l'agent complet, prenez un modèle à outils (Qwen 3, Llama 3.1, Mistral...) chargé avec 16 000 jetons de contexte ou plus." },
	{ "compatible", "Autre service compatible OpenAI", false, "", "", "MEX_AGENT_KEY", "Tout service qui offre le point d'accès /chat/completions : saisissez son adresse, sa clé et le nom du modèle." },
};
const int PROVIDER_COUNT = (int)(sizeof(PROVIDERS) / sizeof(PROVIDERS[0]));

const int MAX_ROUNDS = 40; // appels au modèle pour une seule demande de l'utilisateur
const size_t MAX_RESULT_SIZE = 24000; // caractères d'un résultat d'outil renvoyés au modèle

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

// Questions posées à un serveur local sur ce que sait faire un modèle.
enum ProbeStep {
	PROBE_LMSTUDIO_V1, // GET /api/v1/models (LM Studio 0.4 et suivants)
	PROBE_LMSTUDIO_V0, // GET /api/v0/models (LM Studio 0.3)
	PROBE_OLLAMA, // POST /api/show
};

// MEX_AGENT_JOURNAL=<fichier> : chaque requête et chaque réponse y sont écrites (jamais la
// clé d'API). Sert à comprendre un refus d'un serveur local, et aux essais automatiques.
void _journal(const String &p_text) {
	const char *path = SDL_getenv("MEX_AGENT_JOURNAL");
	if (!path || !path[0]) {
		return;
	}
	if (SDL_RWops *file = SDL_RWFromFile(path, "ab")) {
		SDL_RWwrite(file, p_text.data(), 1, p_text.size());
		SDL_RWwrite(file, "\n", 1, 1);
		SDL_RWclose(file);
	}
}

} // namespace

struct EditorAgent::Job {
	enum Kind {
		CHAT, // requête de conversation
		MODELS, // « Lister »
		PROBE, // capacités du modèle d'un serveur local
	};
	std::thread thread;
	std::atomic<bool> done{ false };
	std::atomic<bool> cancel{ false };
	Kind kind = CHAT;
	int probe_step = 0;
	String url;
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
	// Adresse de base, sans « / » final : « /v1 » ajouté une seule fois aux serveurs locaux,
	// point d'accès collé en entier ramené à sa base (voir EditorAgentProtocol::normalize_url).
	const String typed = str_strip(p_settings.url);
	String url = EditorAgentProtocol::normalize_url(typed.empty() ? String(p_provider.default_url) : typed);
	if (!typed.empty() && p_provider.default_url[0]) {
		// Adresse officielle saisie sans son chemin (« https://api.openai.com ») : on le rétablit.
		const String official = EditorAgentProtocol::normalize_url(p_provider.default_url);
		if (str_to_lower(url) == str_to_lower(EditorAgentProtocol::server_root(official))) {
			url = official;
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

String EditorAgent::_system_prompt(bool p_tools) const {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	String text;
	if (p_tools) {
		text =
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
	} else {
		// Modèle sans appel d'outils : il conseille et écrit du code, l'utilisateur agit.
		text =
				"Tu es MEX Agent, l'assistant de création de jeux intégré à l'éditeur MEX. MEX est un moteur de jeu 2D et 3D "
				"avec son propre langage de script, MexScript (fichiers .mx), ses scènes en texte (.scn) et un fichier de "
				"projet (project.mex).\n\n"
				"# Règles\n"
				"- Dans cette conversation, tu n'as AUCUN outil : tu ne peux ni lire les fichiers du projet, ni les modifier, "
				"ni lancer le jeu. Ne prétends jamais l'avoir fait et n'invente pas le contenu d'un fichier : si tu as besoin "
				"d'un script ou d'un message d'erreur, demande à l'utilisateur de le coller.\n"
				"- Aide par la conversation : explique, propose une démarche, et écris le code complet à copier en disant "
				"dans quel fichier le mettre (chemin res://...).\n"
				"- MexScript ressemble à GDScript mais n'est PAS GDScript : indentation par tabulations ; pas de `await` "
				"(utilise un nœud Timer et son signal `timeout`), pas de classes internes, pas d'accesseurs set/get, pas de "
				"`%NomUnique`, pas d'opérateur `**` (utilise `pow`), lambdas sur une seule ligne. MEX n'a pas toute l'API de "
				"Godot : en cas de doute sur une classe ou une méthode, dis-le ; la « Référence des classes » est dans le "
				"menu Aide de l'éditeur (F1).\n"
				"- Réponds dans la langue de l'utilisateur, simplement et brièvement.\n\n";
	}
	text += "# Projet ouvert\n";
	text += "- Nom : " + ps->get_setting("application/name").as_string() + "\n";
	const String main_scene = ps->get_setting("application/main_scene").as_string();
	text += "- Scène principale : " + (main_scene.empty() ? String("(aucune pour l'instant)") : main_scene) + "\n";
	text += "- Fenêtre du jeu : " + ps->get_setting("display/width").as_string() + " x " + ps->get_setting("display/height").as_string() + " pixels\n";
	text += "- Scène ouverte dans l'éditeur : " + (editor->scene_path.empty() ? String("(aucune ou non enregistrée)") : editor->scene_path) + "\n\n";
	if (p_tools) {
		text += "# Compétences disponibles (outil `charger_competence`)\n";
		for (const Skill &skill : skills) {
			text += "- `" + skill.name + "` : " + skill.description + "\n";
		}
	}
	return text;
}

// ---------------------------------------------------------------------------
// Requêtes

String EditorAgent::_profile_key(const Provider &p_provider, const ProviderSettings &p_settings) const {
	return _effective_url(p_provider, p_settings) + "\n" + _effective_model(p_provider, p_settings);
}

EditorAgent::ModelProfile &EditorAgent::_profile() {
	const String key = _profile_key(PROVIDERS[provider_index], _settings());
	auto found = profiles.find(key);
	if (found == profiles.end()) {
		ModelProfile profile;
		// MEX_AGENT_TOOLS=0 : jamais d'outils ; =1 : toujours, sans interroger le serveur.
		if (const char *forced = SDL_getenv("MEX_AGENT_TOOLS")) {
			if (forced[0] == '0' || forced[0] == '1') {
				profile.tools = forced[0] == '1' ? EditorAgentProtocol::TOOLS_YES : EditorAgentProtocol::TOOLS_NO;
				profile.probed = true;
				profile.locked = true;
				profile.origin = "réglage MEX_AGENT_TOOLS";
			}
		}
		found = profiles.emplace(key, profile).first;
	}
	return found->second;
}

Dictionary EditorAgent::build_request(const Provider &p_provider, const ProviderSettings &p_settings) const {
	EditorAgentProtocol::Dialect dialect;
	if (!p_provider.anthropic_format) {
		dialect.local = EditorAgentProtocol::is_local_url(_effective_url(p_provider, p_settings));
		const auto found = profiles.find(_profile_key(p_provider, p_settings));
		if (found != profiles.end()) {
			dialect.tools = found->second.tools != EditorAgentProtocol::TOOLS_NO;
			dialect.system_role = found->second.system_role;
		}
	}
	return EditorAgentProtocol::build_request(p_provider.anthropic_format, _effective_model(p_provider, p_settings), _system_prompt(dialect.tools), messages, EditorAgentTools::get_definitions(), max_tokens, dialect);
}

bool EditorAgent::parse_response(const Provider &p_provider, const String &p_body, Message &r_message, String &r_error, String &r_stop_reason) const {
	// Identifiants d'appels déjà employés : un doublon venu du modèle est renommé.
	std::set<String> used;
	for (const Message &message : messages) {
		for (const ToolCall &call : message.calls) {
			used.insert(call.id);
		}
	}
	return EditorAgentProtocol::parse_response(p_provider.anthropic_format, p_body, 0, used, r_message, r_error, r_stop_reason);
}

void EditorAgent::_notice(const String &p_text) {
	_journal("*** " + p_text);
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

void EditorAgent::list_models() {
	if (listing_models || state == REQUESTING || job) {
		return;
	}
	const String url = _effective_url(PROVIDERS[provider_index], _settings());
	if (url.empty()) {
		_notice("Aucune adresse de service. Saisissez l'adresse du service avant de lister ses modèles.");
		return;
	}
	listing_models = true;
	_start_job(url + "/models", String(), Job::MODELS);
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
	// Serveur local : on lui demande une fois ce que le modèle sait faire (outils, contexte).
	if (!provider.anthropic_format && !_profile().probed && EditorAgentProtocol::is_local_url(url)) {
		_start_probe();
		return;
	}
	_send_request();
}

void EditorAgent::_send_request() {
	const Provider &provider = PROVIDERS[provider_index];
	const ProviderSettings &settings = _settings();
	const String url = _effective_url(provider, settings);
	const String model = _effective_model(provider, settings);
	ModelProfile &profile = _profile();
	if (!provider.anthropic_format && profile.tools == EditorAgentProtocol::TOOLS_NO && !profile.announced) {
		profile.announced = true;
		_notice("« " + model + " » ne sait pas appeler d'outils" + (profile.origin.empty() ? String() : " (" + profile.origin + ")") + ". MEX Agent discute avec lui, mais il ne peut ni lire ni modifier le projet, ni lancer le jeu : il vous donne le code à copier. Pour l'agent complet, choisissez un modèle qui gère les outils (par exemple Qwen 3, Llama 3.1 ou Mistral).");
	}
	const String body = JSON::stringify(Variant(build_request(provider, settings)));
	if (profile.context > 0 && !profile.context_warned && EditorAgentProtocol::estimate_tokens(body) > profile.context) {
		profile.context_warned = true;
		_notice("« " + model + " » est chargé avec un contexte de " + std::to_string(profile.context) + " jetons et cette requête en compte environ " + std::to_string(EditorAgentProtocol::estimate_tokens(body)) + " : le serveur va couper la conversation ou la refuser. Rechargez le modèle avec un contexte plus grand (LM Studio : « Context Length » ; 16 000 jetons ou plus pour l'agent complet).");
	}
	state = REQUESTING;
	status = "Le modèle réfléchit...";
	_start_job(url + (provider.anthropic_format ? "/messages" : "/chat/completions"), body, Job::CHAT);
}

void EditorAgent::_start_probe() {
	const String root = EditorAgentProtocol::server_root(_effective_url(PROVIDERS[provider_index], _settings()));
	// Port habituel d'Ollama : on commence par lui. Sinon par LM Studio.
	if (str_ends_with(root, ":11434")) {
		probe_queue = { PROBE_OLLAMA, PROBE_LMSTUDIO_V1, PROBE_LMSTUDIO_V0 };
	} else {
		probe_queue = { PROBE_LMSTUDIO_V1, PROBE_LMSTUDIO_V0, PROBE_OLLAMA };
	}
	probe_unreachable = false;
	state = REQUESTING;
	status = "Interrogation du serveur local...";
	_probe_next();
}

void EditorAgent::_probe_next() {
	if (probe_queue.empty()) {
		// Capacités connues, ou serveur qui ne les déclare pas (llama.cpp, vLLM...) : dans ce
		// cas les outils sont envoyés, et un refus du serveur fait passer en conversation.
		// Serveur injoignable : rien n'est retenu, la question sera reposée à la prochaine demande.
		_profile().probed = !probe_unreachable;
		_send_request();
		return;
	}
	const int step = probe_queue.front();
	probe_queue.erase(probe_queue.begin());
	const Provider &provider = PROVIDERS[provider_index];
	const String root = EditorAgentProtocol::server_root(_effective_url(provider, _settings()));
	if (step == PROBE_OLLAMA) {
		Dictionary question;
		question[Variant("model")] = Variant(_effective_model(provider, _settings()));
		_start_job(root + "/api/show", JSON::stringify(Variant(question)), Job::PROBE);
	} else {
		_start_job(root + (step == PROBE_LMSTUDIO_V1 ? "/api/v1/models" : "/api/v0/models"), String(), Job::PROBE);
	}
	if (job) {
		job->probe_step = step;
	}
}

void EditorAgent::_handle_probe(const Job &p_job) {
	if (!p_job.started || p_job.exit_code != 0) {
		// Serveur injoignable : la requête de conversation le dira à l'utilisateur.
		probe_queue.clear();
		probe_unreachable = true;
		_probe_next();
		return;
	}
	String body = p_job.output;
	const int http = _split_status(body);
	EditorAgentProtocol::ToolSupport support = EditorAgentProtocol::TOOLS_UNKNOWN;
	int context = 0;
	if (http == 200) {
		if (p_job.probe_step == PROBE_OLLAMA) {
			support = EditorAgentProtocol::ollama_tool_support(body);
		} else {
			support = EditorAgentProtocol::lmstudio_tool_support(body, _effective_model(PROVIDERS[provider_index], _settings()), &context);
		}
	}
	ModelProfile &profile = _profile();
	if (context > 0) {
		profile.context = context;
	}
	if (support != EditorAgentProtocol::TOOLS_UNKNOWN) {
		profile.tools = support;
		profile.origin = p_job.probe_step == PROBE_OLLAMA ? "d'après Ollama" : "d'après LM Studio";
		probe_queue.clear();
	}
	_probe_next();
}

// Le service a refusé la requête pour une raison qui tient au modèle : on retire ce qu'il ne
// sait pas lire (outils, puis rôle « system ») et on renvoie la même conversation. Chaque
// retrait est définitif pour ce modèle : deux nouvelles tentatives au plus.
bool EditorAgent::_adapt_to_model(int p_error_kind) {
	if (PROVIDERS[provider_index].anthropic_format) {
		return false;
	}
	ModelProfile &profile = _profile();
	if (profile.locked) {
		return false;
	}
	const bool has_tools = profile.tools != EditorAgentProtocol::TOOLS_NO;
	switch (p_error_kind) {
		case EditorAgentProtocol::ERROR_NO_TOOLS:
			if (has_tools) {
				profile.tools = EditorAgentProtocol::TOOLS_NO;
				profile.origin = "le service l'a indiqué";
				return true;
			}
			return false;
		case EditorAgentProtocol::ERROR_SYSTEM_ROLE:
			if (profile.system_role) {
				profile.system_role = false;
				return true;
			}
			if (has_tools) {
				profile.tools = EditorAgentProtocol::TOOLS_NO;
				profile.origin = "son gabarit de conversation n'accepte que les rôles « user » et « assistant »";
				return true;
			}
			return false;
		case EditorAgentProtocol::ERROR_TEMPLATE:
			if (has_tools) {
				profile.tools = EditorAgentProtocol::TOOLS_NO;
				profile.origin = "son gabarit de conversation a refusé la requête avec outils";
				return true;
			}
			if (profile.system_role) {
				profile.system_role = false;
				return true;
			}
			return false;
		default:
			return false;
	}
}

void EditorAgent::_start_job(const String &p_url, const String &p_body, int p_kind) {
	const Provider &provider = PROVIDERS[provider_index];
	const String key = _effective_key(provider, _settings());
	// La clé voyage sur l'entrée standard de curl : elle n'apparaît ni dans la liste des
	// processus ni dans un fichier. Le corps de la requête passe par un fichier temporaire du
	// dossier de préférences, désigné par un nom relatif (aucun souci d'accents dans le chemin).
	const String directory = editor->prefs_dir;
	const String body_name = "agent_requete.json";
	const bool has_body = !p_body.empty();
	String config = "url = " + _curl_quote(p_url) + "\nsilent\nshow-error\nlocation\n";
	// Les questions posées à un serveur local ne doivent pas faire attendre.
	config += p_kind == Job::PROBE ? "connect-timeout = 5\nmax-time = 15\n" : "connect-timeout = 30\nmax-time = 900\n";
	if (EditorAgentProtocol::is_local_url(p_url)) {
		// Un serveur de la machine ou du réseau local ne se joint pas par le mandataire (proxy).
		config += "noproxy = \"*\"\n";
	}
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
	if (has_body) {
		config += "request = \"POST\"\ndata-binary = \"@" + body_name + "\"\n";
		if (FileAccess::write_string(directory + body_name, p_body) != OK) {
			_notice("Impossible d'écrire la requête dans " + directory);
			state = IDLE;
			listing_models = false;
			return;
		}
	}
	_journal(String(has_body ? ">>> POST " : ">>> GET ") + p_url + (has_body ? "\n" + p_body : String()));
	if (job) {
		// Une seule requête à la fois : celle qui restait (liste des modèles) est abandonnée.
		job->cancel = true;
		if (job->thread.joinable()) {
			job->thread.join();
		}
		listing_models = false;
	}
	job = std::make_unique<Job>();
	job->kind = (Job::Kind)p_kind;
	job->url = p_url;
	Job *current = job.get();
	current->thread = std::thread([current, config, directory, body_name, has_body]() {
		current->started = Process::run("curl", { "--config", "-" }, config, &current->output, &current->errors, &current->exit_code, &current->cancel, directory);
		if (has_body) {
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
	// Les capacités des modèles sont redemandées : un autre modèle a pu être chargé.
	profiles.clear();
	probe_queue.clear();
}

void EditorAgent::_poll_job() {
	if (!job || !job->done) {
		return;
	}
	if (job->thread.joinable()) {
		job->thread.join();
	}
	const std::unique_ptr<Job> finished = std::move(job);
	if (!finished->cancel) {
		_journal("<<< " + finished->url + (finished->started && finished->exit_code == 0 ? "\n" + finished->output : "\ncurl : " + str_strip(finished->errors) + " (code " + std::to_string(finished->exit_code) + ")"));
	}
	if (finished->kind == Job::MODELS) {
		listing_models = false;
		_handle_models(*finished);
		return;
	}
	if (finished->cancel) {
		probe_queue.clear();
		if (state == REQUESTING) {
			state = IDLE;
			_notice("Interrompu.");
		}
		return;
	}
	if (finished->kind == Job::PROBE) {
		_handle_probe(*finished);
		return;
	}
	_handle_response(*finished);
}

namespace {

String _curl_failure(bool p_started, int p_exit_code, const String &p_errors, const String &p_url) {
	if (!p_started) {
		return "Le programme « curl » est introuvable. MEX Agent s'en sert pour joindre le service. Il est fourni avec Windows 10 (version 1803) et suivants et avec macOS ; sous Linux, installez le paquet « curl ».";
	}
	String reason = str_strip(p_errors);
	if (reason.empty()) {
		reason = "code " + std::to_string(p_exit_code);
	}
	if (EditorAgentProtocol::is_local_url(p_url)) {
		return "Connexion impossible à " + p_url + " (" + reason + "). Vérifiez que le serveur local est démarré et que l'adresse est la bonne. LM Studio : onglet « Developer », « Start Server », adresse http://localhost:1234. Ollama : adresse http://localhost:11434.";
	}
	return "Connexion impossible (" + reason + "). Vérifiez votre connexion Internet et l'adresse du service.";
}

// Conseil qui accompagne un refus du service, selon sa cause.
String _failure_hint(int p_kind, int p_http, const String &p_url, const String &p_model) {
	switch (p_kind) {
		case EditorAgentProtocol::ERROR_ENDPOINT:
			return "\nAdresse appelée : " + p_url + "\nLe service ne connaît pas ce chemin : corrigez l'adresse dans « Connexion ». Elle se termine en général par /v1 (LM Studio : http://localhost:1234/v1 ; Ollama : http://localhost:11434/v1)." + (p_http == 404 && !p_model.empty() ? " Si l'adresse est bonne, c'est le modèle « " + p_model + " » qui n'existe pas : « Lister » montre ceux du service." : String());
		case EditorAgentProtocol::ERROR_MODEL:
			return "\nLe modèle « " + p_model + " » est inconnu du service ou n'est pas chargé : « Lister » montre les modèles disponibles.";
		case EditorAgentProtocol::ERROR_AUTH:
			return "\nLa clé d'API est absente, erronée ou sans droit sur ce modèle.";
		case EditorAgentProtocol::ERROR_RATE:
			return "\nLimite de débit ou crédit épuisé chez le fournisseur.";
		case EditorAgentProtocol::ERROR_CONTEXT:
			return "\nLa conversation dépasse la fenêtre de contexte du modèle. Rechargez-le avec un contexte plus grand (LM Studio : « Context Length » ; Ollama : variable OLLAMA_CONTEXT_LENGTH) ou commencez une nouvelle conversation.";
		case EditorAgentProtocol::ERROR_NO_TOOLS:
			return "\nCe modèle ne sait pas appeler d'outils.";
		case EditorAgentProtocol::ERROR_SYSTEM_ROLE:
		case EditorAgentProtocol::ERROR_TEMPLATE:
			return "\nLe gabarit de conversation (« prompt template ») du modèle a refusé la requête, même sans outils et sans message système. Vérifiez le gabarit du modèle dans le serveur local, ou essayez un autre modèle.";
		default:
			return String();
	}
}

} // namespace

void EditorAgent::_handle_response(const Job &p_job) {
	status.clear();
	const Provider &provider = PROVIDERS[provider_index];
	if (!p_job.started || p_job.exit_code != 0) {
		_notice(_curl_failure(p_job.started, p_job.exit_code, p_job.errors, p_job.url));
		state = IDLE;
		return;
	}
	String body = p_job.output;
	const int http = _split_status(body);
	Message message;
	String error, stop_reason;
	std::set<String> used_ids;
	for (const Message &existing : messages) {
		for (const ToolCall &call : existing.calls) {
			used_ids.insert(call.id);
		}
	}
	if (!EditorAgentProtocol::parse_response(provider.anthropic_format, body, http, used_ids, message, error, stop_reason)) {
		const EditorAgentProtocol::ErrorKind kind = EditorAgentProtocol::classify_error(http, error);
		if (_adapt_to_model(kind)) {
			// Même conversation, dans la forme que le modèle accepte.
			_send_request();
			return;
		}
		String text = http >= 400 ? "Le service a refusé la requête (HTTP " + std::to_string(http) + ")" : String("Le service n'a pas répondu comme prévu");
		text += " : " + error;
		text += _failure_hint(kind, http, p_job.url, _effective_model(provider, _settings()));
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
	if (message.calls.empty() && message.text.empty()) {
		// Rien à garder : un message vide casserait l'alternance de la conversation.
		_notice(truncated ? "La réponse a été coupée par la limite de jetons avant le premier mot. Écrivez « continue », ou rechargez le modèle avec un contexte plus grand." : "Le modèle a renvoyé une réponse vide. Reformulez la demande ou écrivez « continue ».");
		state = IDLE;
		return;
	}
	messages.push_back(message);
	scroll_to_end = 3;
	if (messages.back().calls.empty()) {
		if (truncated) {
			_notice(provider.anthropic_format ? "La réponse a été coupée par la limite de jetons (" + std::to_string(max_tokens) + "). Augmentez « Jetons max. » dans « Connexion » ou écrivez « continue »." : String("La réponse a été coupée par la limite de jetons du service. Écrivez « continue »."));
		}
		state = IDLE;
		return;
	}
	state = TOOLS;
}

void EditorAgent::_handle_models(const Job &p_job) {
	if (!p_job.started || p_job.exit_code != 0) {
		_notice(_curl_failure(p_job.started, p_job.exit_code, p_job.errors, p_job.url));
		return;
	}
	String body = p_job.output;
	const int http = _split_status(body);
	std::vector<String> names;
	String error;
	if (!EditorAgentProtocol::parse_models(body, http, names, error)) {
		const EditorAgentProtocol::ErrorKind kind = EditorAgentProtocol::classify_error(http, error);
		_notice("Impossible de lister les modèles" + (http >= 400 ? " (HTTP " + std::to_string(http) + ")" : String()) + " : " + error + (kind == EditorAgentProtocol::ERROR_MODEL || kind == EditorAgentProtocol::ERROR_NO_TOOLS ? String() : _failure_hint(kind, 0, p_job.url, String())));
		return;
	}
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
		list_models();
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
	ImGui::SetItemTooltip("Laisser vide pour l'adresse officielle. À changer pour un relais ou un serveur local.\nServeur local : http://localhost:1234 (LM Studio) ou http://localhost:11434 (Ollama) suffit, « /v1 » est ajouté tout seul.");
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Jetons max.");
	ImGui::SameLine(label_width);
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputInt("##jetons", &max_tokens, 1024, 4096)) {
		max_tokens = std::clamp(max_tokens, 256, 128000);
		changed = true;
	}
	ImGui::SetItemTooltip("Longueur maximale d'une réponse, pour l'API d'Anthropic.\nLes autres services (OpenAI, LM Studio, Ollama...) appliquent leur propre limite :\nce réglage ne leur est pas envoyé.");
	ImGui::SetItemTooltip("Longueur maximale d'une réponse du modèle (Claude). Trop bas, les longs fichiers sont coupés.");
	changed |= ImGui::Checkbox("Demander avant de modifier", &ask_before_changes);
	ImGui::SetItemTooltip("Coché : chaque écriture de fichier et chaque réglage du projet attend votre accord.\nDécoché : MEX Agent écrit sans attendre.\nLe bouton « Annuler les changements » reste disponible.");
	if (changed) {
		save_settings();
	}
}
