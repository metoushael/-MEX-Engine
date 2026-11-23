#pragma once

#include "editor_agent.h"

// Outils que MEX Agent met à la disposition du modèle. Chaque outil agit sur le projet ouvert
// dans l'éditeur ; aucun ne sort du dossier du projet.
class EditorAgentTools {
public:
	// [{ name, description, parameters (schéma JSON) }]
	static Array get_definitions();
	// Vrai pour les outils qui changent le projet (soumis à l'accord de l'utilisateur).
	static bool modifies_project(const String &p_name);
	// Libellé court d'un appel, pour la conversation.
	static String describe(const EditorAgent::ToolCall &p_call);
	// Ce que l'appel va changer, pour la demande d'accord.
	static String describe_change(const EditorAgent::ToolCall &p_call);
	// Exécute l'appel. Renvoie false tant qu'il n'est pas terminé (« lancer_jeu »).
	static bool execute(EditorAgent *p_agent, const EditorAgent::ToolCall &p_call, String &r_result, bool &r_is_error);
	// Met l'éditeur à jour après un changement de fichier (p_path vide : tout).
	static void refresh_editor(EditorAgent *p_agent, const String &p_path);
	// Convertit un chemin donné par le modèle en chemin res:// ; refuse ce qui sort du projet.
	static bool resolve_path(const String &p_input, String &r_path, String &r_error);

	static int get_builtin_skill_count();
	static const char *get_builtin_skill(int p_index);
};
