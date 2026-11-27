#pragma once

#include "core/variant/variant.h"

#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <vector>

class EditorNode;

// « MEX Agent » : assistant de création de jeux dans l'éditeur. Il parle à un modèle de
// langage (API d'Anthropic, d'OpenAI, d'OpenRouter ou tout service compatible), qui agit sur
// le projet par des outils : lire et écrire des fichiers, consulter les classes du moteur,
// régler le projet, lancer le jeu et lire sa sortie.
//
// Avec un serveur local (LM Studio, Ollama), MEX Agent demande au serveur si le modèle sait
// appeler des outils. Sinon, il passe en mode « conversation » : mêmes échanges, sans outils,
// dans une forme que le gabarit du modèle accepte (voir editor_agent_protocol.h).
//
// Les requêtes HTTPS passent par le programme « curl » du système (fourni avec Windows 10 et
// suivants, macOS et la plupart des Linux) : MEX n'embarque pas de bibliothèque TLS.
class EditorAgent {
public:
	struct Provider {
		const char *id;
		const char *label;
		bool anthropic_format; // sinon : format « chat completions » d'OpenAI
		const char *default_url;
		const char *default_model;
		const char *key_variable; // variable d'environnement lue si aucune clé n'est saisie
		const char *help;
	};
	struct ProviderSettings {
		String key;
		String model;
		String url;
		std::vector<String> models; // réponse de « Lister les modèles »
	};
	struct Skill {
		String name;
		String description;
		String body;
		String origin; // "MEX", "projet" ou "utilisateur"
	};
	struct ToolCall {
		enum State {
			WAITING,
			NEEDS_APPROVAL,
			RUNNING,
			DONE,
		};
		String id;
		String name;
		Dictionary arguments;
		String result;
		bool is_error = false;
		State state = WAITING;
		// Champ « extra_content » de l'appel, tel que le service l'a envoyé. Gemini y place la
		// « signature de réflexion » (extra_content.google.thought_signature) et refuse la
		// requête suivante si l'appel revient sans elle. Renvoyé tel quel, au seul service et
		// au seul modèle qui l'ont écrit (extra_origin : adresse, saut de ligne, modèle).
		Dictionary extra;
		String extra_origin;
		// Interface : détails dépliés, instant de la première apparition.
		bool ui_open = false;
		double ui_born = -1.0;
	};
	// Ce que l'on sait d'un modèle sur un service donné (une entrée par adresse et par modèle).
	struct ModelProfile {
		int tools = 0; // EditorAgentProtocol::ToolSupport : inconnu, oui, non
		bool system_role = true; // faux : le gabarit du modèle refuse le rôle « system »
		bool probed = false; // le serveur local a été interrogé
		bool locked = false; // imposé par MEX_AGENT_TOOLS : aucune adaptation
		bool announced = false; // l'absence d'outils a été signalée dans la conversation
		bool context_warned = false;
		bool thought_signatures = false; // Gemini derrière un relais : le service exige une signature par étape d'outils
		int context = 0; // fenêtre de contexte du modèle chargé (0 : inconnue)
		String origin; // d'où vient l'information, pour le message à l'utilisateur
	};
	struct Message {
		enum Kind {
			USER,
			ASSISTANT,
			NOTICE, // message de l'éditeur (erreur de connexion...) : jamais envoyé au modèle
		};
		Kind kind = USER;
		String text;
		std::vector<ToolCall> calls;
		// Interface : instant de la première apparition, nombre d'octets du texte déjà écrits.
		double ui_born = -1.0;
		float ui_revealed = 0.0f;
	};

	explicit EditorAgent(EditorNode *p_editor);
	~EditorAgent();

	void load_settings();
	void save_settings() const;
	void draw(); // fenêtre « MEX Agent »
	void draw_connection_settings(); // aussi affiché dans les préférences de l'éditeur
	void submit(const String &p_text);
	void list_models(); // bouton « Lister » : GET <adresse>/models
	void stop();
	void new_conversation();
	bool is_busy() const { return state != IDLE; }
	bool is_listing_models() const { return listing_models; }
	bool is_open() const { return open; }
	void set_open(bool p_open) { open = p_open; }
	void focus() { open = true; want_focus = true; }
	void open_settings() { open = true; want_focus = true; show_settings = true; }
	// Mode Agent : la conversation occupe la vue centrale de l'éditeur (à la place des vues
	// 2D, 3D, Script et Jeu) ; sinon elle reste un panneau latéral.
	void enter_agent_mode();
	void leave_agent_mode();
	bool is_agent_mode() const { return agent_mode; }

	static int get_provider_count();
	static const Provider &get_provider(int p_index);
	const std::vector<Skill> &get_skills();

	// Construction des requêtes et lecture des réponses, sans réseau. Le travail est fait par
	// EditorAgentProtocol, que vérifie le programme d'essais « mex-essais-agent ».
	Dictionary build_request(const Provider &p_provider, const ProviderSettings &p_settings) const;
	bool parse_response(const Provider &p_provider, const String &p_body, Message &r_message, String &r_error, String &r_stop_reason) const;

private:
	friend class EditorAgentTools;
	enum State {
		IDLE,
		REQUESTING,
		TOOLS,
	};
	struct Job;

	EditorNode *editor = nullptr;
	bool open = true;
	bool want_focus = false;
	bool show_settings = false;
	bool show_key = false;
	int scroll_to_end = 0; // images pendant lesquelles la conversation reste calée en bas
	bool agent_mode = false; // la conversation recouvre la zone de travail
	double mode_born = -1.0; // instant d'entrée dans le mode Agent
	bool game_was_running = false;
	int dock_request = 0; // 2 : ancrer avec l'inspecteur (sortie du mode Agent)
	int dock_tries = 0;
	bool stick_bottom = true; // la vue suit la fin de la conversation
	bool animations = true;
	bool animations_forced_off = false; // MEX_AGENT_ANIMATIONS=0
	bool focus_input = false;
	bool input_active = false;
	double welcome_born = -1.0;
	State state = IDLE;
	int provider_index = 0;
	std::map<String, ProviderSettings> provider_settings;
	int max_tokens = 8192;
	bool ask_before_changes = true;
	int rounds = 0;
	String prompt;
	String status;
	std::vector<Message> messages;
	std::vector<Skill> skills;
	bool skills_loaded = false;
	String skills_project; // projet pour lequel les compétences ont été lues
	std::unique_ptr<Job> job;
	bool listing_models = false;
	std::map<String, ModelProfile> profiles; // clé : adresse du service, saut de ligne, modèle
	std::vector<int> probe_queue; // questions qui restent à poser au serveur local
	bool probe_unreachable = false; // le serveur n'a pas répondu : la question sera reposée
	// Contenu d'origine des fichiers touchés pendant la conversation (false : fichier créé).
	std::map<String, std::pair<bool, String>> originals;
	// Outil « lancer_jeu » en cours.
	double run_remaining = 0;
	int run_console_start = 0;
	bool run_started_game = false;

	ProviderSettings &_settings();
	String _effective_key(const Provider &p_provider, const ProviderSettings &p_settings) const;
	String _effective_url(const Provider &p_provider, const ProviderSettings &p_settings) const;
	String _effective_model(const Provider &p_provider, const ProviderSettings &p_settings) const;
	String _system_prompt(bool p_tools) const;
	String _profile_key(const Provider &p_provider, const ProviderSettings &p_settings) const;
	ModelProfile &_profile();
	void _start_request();
	void _send_request();
	void _start_probe();
	void _probe_next();
	bool _adapt_to_model(int p_error_kind);
	void _start_job(const String &p_url, const String &p_body, int p_kind);
	void _poll_job();
	void _handle_response(const Job &p_job);
	void _handle_probe(const Job &p_job);
	void _handle_models(const Job &p_job);
	void _process_tools();
	void _finish_call(ToolCall &p_call, const String &p_result, bool p_is_error);
	void _notice(const String &p_text);
	void _revert_changes();
	void _load_skills();
	void _draw_message(Message &p_message, int p_index);
	void _draw_welcome(float p_height);
	void _draw_text(const String &p_text);
};
