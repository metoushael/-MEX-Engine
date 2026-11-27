#pragma once

#include "editor_agent.h"

#include <set>

// Protocole de MEX Agent : tout ce qui se décide sans réseau et sans éditeur. Adresses des
// services, construction des requêtes, lecture des réponses et des erreurs, capacités des
// modèles. Ces fonctions ne dépendent que du cœur du moteur : elles sont vérifiées par le
// programme d'essais « mex-essais-agent » (tests/agent/essais_protocole.cpp).
namespace EditorAgentProtocol {

// ---------------------------------------------------------------------------
// Adresses

// Adresse de base d'un service, à partir de ce que l'utilisateur a saisi. Le résultat ne se
// termine jamais par « / » ; on lui ajoute « /chat/completions », « /messages » ou « /models ».
//   localhost:1234                              -> http://localhost:1234/v1
//   http://localhost:1234/v1/                   -> http://localhost:1234/v1
//   http://localhost:1234/v1/v1                 -> http://localhost:1234/v1
//   http://localhost:1234/v1/chat/completions   -> http://localhost:1234/v1
//   http://localhost:1234/models                -> http://localhost:1234/v1
//   http://localhost:11434/api/chat             -> http://localhost:11434/v1
//   https://api.groq.com/openai/v1/models       -> https://api.groq.com/openai/v1
//   https://generativelanguage.googleapis.com/v1beta/openai   (inchangée)
// « /v1 » n'est ajouté qu'aux serveurs locaux (LM Studio, Ollama, llama.cpp... : tous servent
// leur API compatible avec OpenAI sous /v1). L'adresse d'un service distant garde son chemin :
// certains n'ont pas de numéro de version (GitHub Models, Azure).
String normalize_url(const String &p_text);
// « http://hôte:port » d'une adresse de base : racine des API propres à LM Studio (/api/v0)
// et à Ollama (/api).
String server_root(const String &p_base_url);
// Vrai pour un serveur de la machine ou du réseau local (localhost, 127.x, 192.168.x...).
bool is_local_url(const String &p_url);

// ---------------------------------------------------------------------------
// Requêtes

// Ce que le modèle accepte dans une requête « chat completions ». Les modèles locaux ne
// prennent pas tous en charge les outils, ni même le rôle « system » : leur gabarit de
// conversation (Jinja) refuse alors la requête.
struct Dialect {
	bool tools = true; // champ « tools », appels d'outils, messages de rôle « tool »
	bool system_role = true; // sinon : les instructions ouvrent le premier message de l'utilisateur
	// Serveur local (LM Studio, Ollama, llama.cpp) : un message sans texte porte "" et non
	// null, que les gabarits Jinja des modèles ne savent pas lire.
	bool local = false;
	// Service et modèle de la requête (adresse, saut de ligne, modèle) : le champ
	// « extra_content » d'un appel d'outil n'est renvoyé qu'à celui qui l'a écrit.
	String origin;
	// Gemini (à partir de la version 3) signe le premier appel d'outil de chaque étape et
	// refuse la suite si la signature ne revient pas. Vrai : une étape sans signature (autre
	// modèle, autre service) reçoit la valeur de remplacement prévue par Google.
	bool thought_signatures = false;
};

// Valeur que Google documente pour un appel d'outil que le modèle n'a pas signé lui-même.
extern const char *const THOUGHT_SIGNATURE_PLACEHOLDER;
// Vrai pour les adresses de Google (API Gemini, Vertex AI), qui exigent ces signatures.
bool wants_thought_signatures(const String &p_base_url);

// Corps de la requête. p_definitions : [{ name, description, parameters }].
// Garanties, pour les deux formats : aucun message vide, jamais deux messages « user » ou
// « assistant » de suite, un résultat pour chaque appel d'outil, dans l'ordre des appels et
// sous un identifiant unique, jamais de message « user » juste après un résultat d'outil.
// Sans outils (p_dialect.tools faux), la conversation est une alternance stricte
// user / assistant ; les anciens appels d'outils y figurent sous forme de texte.
Dictionary build_request(bool p_anthropic_format, const String &p_model, const String &p_system, const std::vector<EditorAgent::Message> &p_messages, const Array &p_definitions, int p_max_tokens, const Dialect &p_dialect);

// ---------------------------------------------------------------------------
// Réponses

// Lit la réponse du service. p_used_ids : identifiants d'appels d'outils déjà employés dans
// la conversation ; un identifiant absent ou déjà pris est remplacé (9 caractères
// alphanumériques, la seule forme que tous les services acceptent).
bool parse_response(bool p_anthropic_format, const String &p_body, int p_http_status, std::set<String> &p_used_ids, EditorAgent::Message &r_message, String &r_error, String &r_stop_reason);

// Message d'erreur contenu dans une réponse, quel que soit le service ; texte vide s'il n'y en
// a pas. Une page HTML ou un texte brut sont résumés.
String extract_error(const String &p_body, int p_http_status);

enum ErrorKind {
	ERROR_OTHER,
	ERROR_ENDPOINT, // mauvais chemin : « Unexpected endpoint or method » de LM Studio, 404, 405
	ERROR_NO_TOOLS, // le modèle ne sait pas appeler d'outils
	ERROR_SYSTEM_ROLE, // le gabarit du modèle refuse le rôle « system »
	ERROR_TEMPLATE, // autre refus du gabarit de conversation (alternance des rôles...)
	ERROR_CONTEXT, // la conversation dépasse la fenêtre de contexte
	ERROR_MODEL, // modèle inconnu ou non chargé
	ERROR_AUTH,
	ERROR_RATE,
	ERROR_SIGNATURE, // Gemini : « Function call is missing a thought_signature »
};
ErrorKind classify_error(int p_http_status, const String &p_text);

// Réponse de « GET /models » : noms triés, sans doublon. r_error est rempli si la liste est
// illisible ou vide.
bool parse_models(const String &p_body, int p_http_status, std::vector<String> &r_names, String &r_error);

// ---------------------------------------------------------------------------
// Capacités des modèles locaux

enum ToolSupport {
	TOOLS_UNKNOWN,
	TOOLS_YES,
	TOOLS_NO,
};
// Réponse de « GET /api/v0/models » de LM Studio. r_context reçoit la taille du contexte avec
// laquelle le modèle est chargé (0 si elle est inconnue).
ToolSupport lmstudio_tool_support(const String &p_body, const String &p_model, int *r_context = nullptr);
// Réponse de « POST /api/show » d'Ollama.
ToolSupport ollama_tool_support(const String &p_body);

// ---------------------------------------------------------------------------
// Divers

// Identifiant d'appel d'outil libre dans p_used_ids (qui le reçoit) : p_proposed s'il convient.
String unique_call_id(const String &p_proposed, std::set<String> &p_used_ids);
// Retire les blocs de réflexion « <think>...</think> » que certains modèles locaux écrivent
// dans leur réponse.
String strip_reasoning(const String &p_text);
// Nombre approximatif de jetons d'un texte (pour comparer à la fenêtre de contexte).
int estimate_tokens(const String &p_text);

} // namespace EditorAgentProtocol
