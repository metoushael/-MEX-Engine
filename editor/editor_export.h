// Export d'un jeu : préréglages (export_presets.cfg), modèles d'exécutable par système,
// archive .pck à part ou incorporée à l'exécutable, paquet .app pour macOS.
//
// L'organisation reprend celle de Godot Engine (editor/export/ : EditorExportPreset,
// EditorExportPlatformPC::export_project, save_pack avec « embed »), dont le format de
// export_presets.cfg est aussi repris. Godot Engine est publié sous licence MIT : voir
// thirdparty/godot/LICENSE.txt.
//
// Ce fichier ne dépend pas de l'interface de l'éditeur : il sert aussi à l'export en ligne
// de commande (mex --path <projet> --export "<préréglage>").
#pragma once

#include "core/typedefs.h"

#include <vector>

struct ExportPreset {
	String name;
	String platform; // « Windows Desktop », « Linux », « macOS » (noms de Godot)
	// Exécutable produit (ou paquet .app), relatif au dossier du projet ou absolu.
	String export_path;
	// Listes séparées par des virgules, avec « * » et « ? » : « *.psd, brouillons/* ».
	String include_filter;
	String exclude_filter;
	String custom_template; // vide : modèle trouvé automatiquement
	bool embed_pck = false; // archive incorporée à l'exécutable (un seul fichier à distribuer)
	String bundle_identifier; // macOS : identifiant du paquet, « com.exemple.jeu »
};

class EditorExport {
public:
	static const char *PRESETS_FILE; // « export_presets.cfg », à la racine du projet

	static std::vector<String> get_platforms();
	static String get_host_platform();
	// Préréglage neuf pour ce système, avec un chemin d'export par défaut.
	static ExportPreset make_preset(const String &p_platform, const String &p_game_name);

	static std::vector<ExportPreset> load_presets(const String &p_project_dir);
	static Error save_presets(const String &p_project_dir, const std::vector<ExportPreset> &p_presets);

	// Exécutable « mex-jeu » du système visé ; vide s'il est introuvable. Cherché dans
	// templates/<système>/ à côté de l'éditeur, puis dans le dossier de préférences
	// (export_templates/<système>/), puis à côté de l'éditeur si le système est le même.
	static String find_template(const String &p_platform);
	// Dossier attendu pour le modèle d'un système, à côté de l'éditeur (pour l'aide).
	static String get_template_hint(const String &p_platform);

	// « * » et « ? », sans tenir compte de la casse ; essayé sur le chemin et sur le nom.
	static bool match_filter(const String &p_path, const String &p_filter_list);

	static bool export_preset(const String &p_project_dir, const ExportPreset &p_preset, String &r_message);
};
