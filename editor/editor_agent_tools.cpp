#include "editor_agent_tools.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/variant_parser.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"
#include "editor_node.h"
#include "editor_project_icon.h"
#include "modules/mexscript/mexscript_parser.h"
#include "scene/resources/packed_scene.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "imgui.h"

// Fiches de compétences incorporées (fichier produit par CMake depuis editor/agent/competences).
extern const int MEX_AGENT_SKILL_COUNT;
extern const char *const MEX_AGENT_SKILLS[];

int EditorAgentTools::get_builtin_skill_count() {
	return MEX_AGENT_SKILL_COUNT;
}

const char *EditorAgentTools::get_builtin_skill(int p_index) {
	return MEX_AGENT_SKILLS[p_index];
}

namespace {

struct Parameter {
	const char *name;
	const char *type;
	const char *description;
	bool required;
};

Dictionary _tool(const char *p_name, const char *p_description, std::initializer_list<Parameter> p_parameters) {
	Dictionary properties;
	Array required;
	for (const Parameter &parameter : p_parameters) {
		Dictionary property;
		property[Variant("type")] = Variant(parameter.type);
		property[Variant("description")] = Variant(parameter.description);
		properties[Variant(parameter.name)] = Variant(property);
		if (parameter.required) {
			required.push_back(Variant(parameter.name));
		}
	}
	Dictionary schema;
	schema[Variant("type")] = Variant("object");
	schema[Variant("properties")] = Variant(properties);
	schema[Variant("required")] = Variant(required);
	Dictionary tool;
	tool[Variant("name")] = Variant(p_name);
	tool[Variant("description")] = Variant(p_description);
	tool[Variant("parameters")] = Variant(schema);
	return tool;
}

String _argument(const EditorAgent::ToolCall &p_call, const char *p_name) {
	const Variant value = p_call.arguments.get(Variant(p_name), Variant());
	return value.get_type() == Variant::NIL ? String() : value.as_string();
}

int _count_lines(const String &p_text) {
	if (p_text.empty()) {
		return 0;
	}
	return (int)std::count(p_text.begin(), p_text.end(), '\n') + (p_text.back() == '\n' ? 0 : 1);
}

String _extension(const String &p_path) {
	const size_t slash = p_path.rfind('/');
	const size_t dot = p_path.rfind('.');
	if (dot == String::npos || (slash != String::npos && dot < slash)) {
		return String();
	}
	return str_to_lower(p_path.substr(dot + 1));
}

bool _is_text_extension(const String &p_extension) {
	static const char *const ALLOWED[] = { "mx", "scn", "res", "md", "txt", "csv", "cfg", "json", "shader", "mexignore", "gitignore" };
	for (const char *allowed : ALLOWED) {
		if (p_extension == allowed) {
			return true;
		}
	}
	return false;
}

String _os_path(const String &p_res_path) {
	return ProjectSettings::get_singleton()->globalize_path(p_res_path);
}

void _walk(const String &p_folder, int p_depth, std::vector<String> &r_lines, int &r_skipped) {
	std::vector<String> files, dirs;
	FileAccess::list_dir(p_folder, &files, &dirs);
	std::sort(files.begin(), files.end());
	std::sort(dirs.begin(), dirs.end());
	const String prefix = str_ends_with(p_folder, "/") ? p_folder : p_folder + "/";
	for (const String &file : files) {
		if (file.empty() || file[0] == '.') {
			continue;
		}
		if (r_lines.size() >= 500) {
			r_skipped++;
			continue;
		}
		std::error_code ec;
		const auto size = std::filesystem::file_size(std::filesystem::u8path(_os_path(prefix + file)), ec);
		r_lines.push_back(prefix + file + (ec ? String() : "  (" + std::to_string((unsigned long long)size) + " octets)"));
	}
	for (const String &dir : dirs) {
		if (dir.empty() || dir[0] == '.' || (p_depth == 0 && dir == "export")) {
			continue; // dossiers cachés et jeux exportés
		}
		if (p_depth < 8) {
			_walk(prefix + dir, p_depth + 1, r_lines, r_skipped);
		}
	}
}

void _describe_node(Node *p_node, int p_depth, String &r_out, int &r_count) {
	if (++r_count > 400) {
		return;
	}
	r_out += String((size_t)p_depth * 2, ' ') + p_node->get_name() + " (" + p_node->get_class() + ")";
	if (Resource *script = Object::cast_to<Resource>(p_node->get_script().as_object())) {
		r_out += "  script : " + script->get_path();
	}
	if (p_depth > 0 && !p_node->get_scene_file_path().empty()) {
		r_out += "  instance de " + p_node->get_scene_file_path();
	}
	r_out += "\n";
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_describe_node(p_node->get_child(i), p_depth + 1, r_out, r_count);
	}
}

String _class_documentation(const String &p_class) {
	if (!ClassDB::class_exists(p_class)) {
		std::vector<String> classes;
		ClassDB::get_class_list(&classes);
		std::sort(classes.begin(), classes.end());
		String out = "La classe « " + p_class + " » n'existe pas dans MEX. Classes disponibles :\n";
		for (const String &name : classes) {
			out += name + ", ";
		}
		return out;
	}
	String out;
	String current = p_class;
	bool first = true;
	while (!current.empty()) {
		ClassInfo *info = ClassDB::get_class_info(current);
		if (!info) {
			break;
		}
		const String parent = ClassDB::get_parent_class(current);
		out += first ? "# " + current + (parent.empty() ? String() : " (hérite de " + parent + ")") + "\n" : "\n## Hérité de " + current + "\n";
		first = false;
		String properties;
		for (const PropertyInfo &property : info->property_list) {
			if (property.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_CATEGORY)) {
				continue;
			}
			properties += "  " + property.name + " : " + Variant::get_type_name(property.type);
			if (!property.hint_string.empty() && property.hint_string.size() < 120) {
				properties += "  [" + property.hint_string + "]";
			}
			properties += "\n";
		}
		if (!properties.empty()) {
			out += "Propriétés :\n" + properties;
		}
		std::vector<String> methods;
		for (const auto &method : info->methods) {
			const MethodBind *bind = method.second;
			methods.push_back(method.first + "(" + (bind && bind->is_vararg ? String("...") : (bind && bind->argument_count > 0 ? std::to_string(bind->argument_count) + " argument(s)" : String())) + ")");
		}
		std::sort(methods.begin(), methods.end());
		if (!methods.empty()) {
			out += "Méthodes : ";
			for (size_t i = 0; i < methods.size(); i++) {
				out += (i ? ", " : "") + methods[i];
			}
			out += "\n";
		}
		if (!info->signals.empty()) {
			out += "Signaux : ";
			for (size_t i = 0; i < info->signals.size(); i++) {
				out += (i ? ", " : "") + info->signals[i].name + "(";
				for (size_t a = 0; a < info->signals[i].arguments.size(); a++) {
					out += (a ? ", " : "") + info->signals[i].arguments[a].name;
				}
				out += ")";
			}
			out += "\n";
		}
		if (!info->constants.empty()) {
			std::vector<String> constants;
			for (const auto &constant : info->constants) {
				constants.push_back(constant.first + " = " + std::to_string((long long)constant.second));
			}
			std::sort(constants.begin(), constants.end());
			out += "Constantes : ";
			for (size_t i = 0; i < constants.size(); i++) {
				out += (i ? ", " : "") + constants[i];
			}
			out += "\n";
		}
		current = parent;
	}
	return out;
}

bool _parse_color(const String &p_text, uint8_t r_rgba[4]) {
	String text = str_strip(p_text);
	if (!text.empty() && text[0] == '#') {
		text = text.substr(1);
	}
	if (text.size() != 6 && text.size() != 8) {
		return false;
	}
	r_rgba[3] = 255;
	for (size_t i = 0; i < text.size() / 2; i++) {
		char *end = nullptr;
		const String pair = text.substr(i * 2, 2);
		const long value = strtol(pair.c_str(), &end, 16);
		if (end != pair.c_str() + 2) {
			return false;
		}
		r_rgba[i] = (uint8_t)value;
	}
	return true;
}

} // namespace

Array EditorAgentTools::get_definitions() {
	Array tools;
	tools.push_back(Variant(_tool("lister_fichiers", "Liste les fichiers du projet (récursif), avec leur taille. À utiliser en premier pour connaître le projet.", { { "dossier", "string", "Dossier à lister, par exemple res://niveaux. Par défaut : res:// (tout le projet).", false } })));
	tools.push_back(Variant(_tool("lire_fichier", "Lit un fichier texte du projet : script .mx, scène .scn, project.mex, etc.", { { "chemin", "string", "Chemin du fichier, par exemple res://joueur.mx", true }, { "ligne_debut", "integer", "Première ligne à lire (1 par défaut). Pour les longs fichiers.", false }, { "nombre_lignes", "integer", "Nombre de lignes à lire (tout le fichier par défaut).", false } })));
	tools.push_back(Variant(_tool("ecrire_fichier", "Crée ou remplace entièrement un fichier texte du projet (.mx, .scn, .md, .txt, .csv, .cfg, .json, .shader). Les dossiers manquants sont créés. Un script .mx est vérifié (erreur de syntaxe renvoyée avec sa ligne) et une scène .scn est rechargée (erreur renvoyée si elle est invalide). Ne sert pas pour project.mex : utilise definir_parametre.", { { "chemin", "string", "Chemin du fichier, par exemple res://joueur.mx", true }, { "contenu", "string", "Contenu complet du fichier.", true } })));
	tools.push_back(Variant(_tool("remplacer_dans_fichier", "Remplace un passage exact d'un fichier texte par un autre. À préférer à ecrire_fichier pour un petit changement. Le passage `ancien` doit apparaître une seule fois dans le fichier, à l'identique (tabulations comprises).", { { "chemin", "string", "Chemin du fichier.", true }, { "ancien", "string", "Texte exact à remplacer (unique dans le fichier).", true }, { "nouveau", "string", "Texte de remplacement.", true } })));
	tools.push_back(Variant(_tool("documentation_classe", "Donne les propriétés, méthodes, signaux et constantes réels d'une classe du moteur MEX (héritage compris). À consulter avant d'utiliser une classe : MEX n'a pas toute l'API de Godot. Avec un nom inconnu, renvoie la liste de toutes les classes.", { { "classe", "string", "Nom de la classe, par exemple CharacterBody2D.", true } })));
	tools.push_back(Variant(_tool("charger_competence", "Charge une fiche de savoir-faire (syntaxe exacte, recettes, méthode). Les fiches disponibles sont listées dans les instructions.", { { "nom", "string", "Nom de la compétence, par exemple mexscript.", true } })));
	tools.push_back(Variant(_tool("scene_ouverte", "Montre l'arbre des nœuds de la scène ouverte dans l'éditeur (noms, types, scripts).", {})));
	tools.push_back(Variant(_tool("ouvrir_scene", "Ouvre une scène dans l'éditeur pour que l'utilisateur la voie.", { { "chemin", "string", "Chemin de la scène, par exemple res://niveau.scn", true } })));
	tools.push_back(Variant(_tool("definir_parametre", "Modifie un paramètre du projet et enregistre project.mex. Exemples de noms : application/name, application/main_scene, display/width, display/height, physics/gravity, rendering/clear_color, input/<action> (liste de touches et de boutons de manette), input_devices/pointing/emulate_touch_from_mouse.", { { "nom", "string", "Nom du paramètre, par exemple input/sauter", true }, { "valeur", "string", "Valeur écrite comme dans project.mex : true, 12, 1.5, \"texte\", [\"Space\", \"JoyA\"], Color(0.2, 0.3, 0.5, 1), Vector2(0, 980).", true } })));
	tools.push_back(Variant(_tool("creer_image", "Crée une image PNG unie (rectangle ou disque) qui sert de sprite provisoire. Ne remplace pas une image existante.", { { "chemin", "string", "Chemin du fichier .png à créer.", true }, { "largeur", "integer", "Largeur en pixels (1 à 1024).", true }, { "hauteur", "integer", "Hauteur en pixels (1 à 1024).", true }, { "couleur", "string", "Couleur en hexadécimal, par exemple #3fa7ff.", true }, { "forme", "string", "rectangle (par défaut) ou disque.", false } })));
	tools.push_back(Variant(_tool("lancer_jeu", "Lance le jeu quelques secondes dans l'éditeur, puis l'arrête et renvoie le texte de sa sortie : erreurs, avertissements et print(). Ne montre pas l'image et ne simule aucune touche.", { { "scene", "string", "Scène à lancer. Par défaut : la scène principale du projet.", false }, { "secondes", "number", "Durée en secondes (3 par défaut, 20 au plus).", false } })));
	return tools;
}

bool EditorAgentTools::modifies_project(const String &p_name) {
	return p_name == "ecrire_fichier" || p_name == "remplacer_dans_fichier" || p_name == "definir_parametre" || p_name == "creer_image";
}

String EditorAgentTools::describe(const EditorAgent::ToolCall &p_call) {
	const String path = _argument(p_call, "chemin");
	if (p_call.name == "lister_fichiers") {
		return "Lister les fichiers " + _argument(p_call, "dossier");
	}
	if (p_call.name == "lire_fichier") {
		return "Lire " + path;
	}
	if (p_call.name == "ecrire_fichier") {
		return "Écrire " + path;
	}
	if (p_call.name == "remplacer_dans_fichier") {
		return "Modifier " + path;
	}
	if (p_call.name == "documentation_classe") {
		return "Documentation de " + _argument(p_call, "classe");
	}
	if (p_call.name == "charger_competence") {
		return "Compétence « " + _argument(p_call, "nom") + " »";
	}
	if (p_call.name == "scene_ouverte") {
		return "Regarder la scène ouverte";
	}
	if (p_call.name == "ouvrir_scene") {
		return "Ouvrir " + path;
	}
	if (p_call.name == "definir_parametre") {
		return "Paramètre " + _argument(p_call, "nom");
	}
	if (p_call.name == "creer_image") {
		return "Créer l'image " + path;
	}
	if (p_call.name == "lancer_jeu") {
		return "Lancer le jeu";
	}
	return p_call.name;
}

String EditorAgentTools::describe_change(const EditorAgent::ToolCall &p_call) {
	String path, error;
	resolve_path(_argument(p_call, "chemin"), path, error);
	if (p_call.name == "ecrire_fichier") {
		const int lines = _count_lines(_argument(p_call, "contenu"));
		return String(FileAccess::exists(path) ? "Remplacer " : "Créer ") + path + " (" + std::to_string(lines) + " lignes). Dépliez la ligne ci-dessus pour voir le contenu.";
	}
	if (p_call.name == "remplacer_dans_fichier") {
		return "Modifier un passage de " + path + ". Dépliez la ligne ci-dessus pour voir l'ancien et le nouveau texte.";
	}
	if (p_call.name == "definir_parametre") {
		return "Régler le paramètre du projet « " + _argument(p_call, "nom") + " » sur : " + _argument(p_call, "valeur");
	}
	if (p_call.name == "creer_image") {
		return "Créer l'image " + path + " (" + _argument(p_call, "largeur") + " x " + _argument(p_call, "hauteur") + " pixels, " + _argument(p_call, "couleur") + ").";
	}
	return describe(p_call);
}

bool EditorAgentTools::resolve_path(const String &p_input, String &r_path, String &r_error) {
	String text = str_replace(str_strip(p_input), "\\", "/");
	if (str_begins_with(text, "res://")) {
		text = text.substr(6);
	} else if (text.find(':') != String::npos || str_begins_with(text, "/") || str_begins_with(text, "~")) {
		r_error = "Chemin refusé : seuls les fichiers du projet (res://...) sont accessibles.";
		return false;
	}
	String clean;
	for (const String &part : str_split(text, "/", false)) {
		if (part == ".") {
			continue;
		}
		if (part == "..") {
			r_error = "Chemin refusé : « .. » ferait sortir du projet.";
			return false;
		}
		clean += (clean.empty() ? "" : "/") + part;
	}
	r_path = "res://" + clean;
	return true;
}

void EditorAgentTools::refresh_editor(EditorAgent *p_agent, const String &p_path) {
	EditorNode *editor = p_agent->editor;
	editor->_scan_filesystem();
	// Script ouvert dans l'éditeur de code : on montre le nouveau texte.
	if (!editor->script_path.empty() && (p_path.empty() || p_path == editor->script_path)) {
		Error err = OK;
		const String text = FileAccess::get_file_as_string(editor->script_path, &err);
		if (err == OK) {
			editor->script_text = text;
			editor->script_dirty = false;
			editor->script_error.clear();
			editor->script_error_line = 0;
			editor->script_last_length = (int)text.size();
			editor->script_mtime = FileAccess::get_modified_time(editor->script_path);
		}
	}
	// Scène ouverte : rechargée si elle a changé, ou si un de ses scripts ou sous-scènes a changé.
	if (!editor->scene_path.empty() && !editor->game.is_running()) {
		const String extension = _extension(p_path);
		if (p_path.empty() || p_path == editor->scene_path || extension == "mx" || extension == "scn" || extension == "png") {
			if (FileAccess::exists(editor->scene_path)) {
				editor->_open_scene(editor->scene_path);
			}
		}
	}
}

bool EditorAgentTools::execute(EditorAgent *p_agent, const EditorAgent::ToolCall &p_call, String &r_result, bool &r_is_error) {
	EditorNode *editor = p_agent->editor;
	ProjectSettings *ps = ProjectSettings::get_singleton();
	r_is_error = false;
	const auto fail = [&](const String &p_message) {
		r_result = p_message;
		r_is_error = true;
		return true;
	};
	const auto remember = [&](const String &p_path) {
		if (p_agent->originals.count(p_path)) {
			return;
		}
		if (FileAccess::exists(p_path)) {
			p_agent->originals[p_path] = { true, FileAccess::get_file_as_string(p_path) };
		} else {
			p_agent->originals[p_path] = { false, String() };
		}
	};
	// Vérifie un fichier qui vient d'être écrit ; renvoie le texte à ajouter au résultat.
	const auto check_written = [&](const String &p_path, const String &p_text) {
		const String extension = _extension(p_path);
		if (extension == "mx") {
			MXParseResult parsed;
			MexScriptParser::parse(p_text, parsed);
			if (!parsed.has_error && !ClassDB::class_exists(parsed.extends)) {
				parsed.has_error = true;
				parsed.error = "Classe de base inconnue : '" + parsed.extends + "'.";
				parsed.error_line = 1;
			}
			if (parsed.has_error) {
				r_is_error = true;
				return "\nATTENTION, le fichier est écrit mais le script est INVALIDE : ligne " + std::to_string(parsed.error_line) + " : " + parsed.error + "\nCorrige-le avant de continuer.";
			}
			return String("\nSyntaxe du script : correcte.");
		}
		if (extension == "scn") {
			const size_t before = editor->console_lines.size();
			Ref<PackedScene> packed = ResourceLoader::load(p_path, nullptr, true);
			String messages;
			for (size_t i = std::min(before, editor->console_lines.size()); i < editor->console_lines.size(); i++) {
				if (editor->console_lines[i].level != PRINT_INFO) {
					messages += "\n  " + editor->console_lines[i].text;
				}
			}
			if (packed.is_null() || !packed->can_instantiate()) {
				r_is_error = true;
				return "\nATTENTION, le fichier est écrit mais la scène est INVALIDE (elle ne se charge pas)." + messages + "\nCorrige-la avant de continuer.";
			}
			return "\nLa scène se charge." + (messages.empty() ? String() : "\nMessages au chargement :" + messages);
		}
		return String();
	};

	if (p_call.name == "lister_fichiers") {
		String folder, error;
		if (!resolve_path(_argument(p_call, "dossier"), folder, error)) {
			return fail(error);
		}
		if (!FileAccess::dir_exists(folder)) {
			return fail("Le dossier " + folder + " n'existe pas.");
		}
		std::vector<String> lines;
		int skipped = 0;
		_walk(folder, folder == "res://" ? 0 : 1, lines, skipped);
		if (lines.empty()) {
			r_result = "Aucun fichier dans " + folder + ".";
			return true;
		}
		for (const String &line : lines) {
			r_result += line + "\n";
		}
		if (skipped > 0) {
			r_result += "... et " + std::to_string(skipped) + " autres fichiers (liste un sous-dossier pour les voir).\n";
		}
		return true;
	}

	if (p_call.name == "lire_fichier") {
		String path, error;
		if (!resolve_path(_argument(p_call, "chemin"), path, error)) {
			return fail(error);
		}
		if (!FileAccess::exists(path)) {
			return fail("Le fichier " + path + " n'existe pas. Utilise lister_fichiers pour voir les fichiers du projet.");
		}
		Error err = OK;
		const String text = FileAccess::get_file_as_string(path, &err);
		if (err != OK) {
			return fail("Impossible de lire " + path + ".");
		}
		if (text.substr(0, 4096).find('\0') != String::npos) {
			return fail(path + " est un fichier binaire (" + std::to_string(text.size()) + " octets) : il ne peut pas être lu comme du texte.");
		}
		const int total = _count_lines(text);
		int first = std::max(1, (int)p_call.arguments.get(Variant("ligne_debut"), Variant(1)).as_int());
		int count = (int)p_call.arguments.get(Variant("nombre_lignes"), Variant(0)).as_int();
		if (first == 1 && count <= 0) {
			r_result = text.empty() ? String("(fichier vide)") : text;
			return true;
		}
		if (count <= 0) {
			count = total;
		}
		size_t pos = 0;
		for (int line = 1; line < first && pos != String::npos; line++) {
			pos = text.find('\n', pos);
			if (pos != String::npos) {
				pos++;
			}
		}
		if (pos == String::npos || pos >= text.size()) {
			return fail(path + " n'a que " + std::to_string(total) + " lignes.");
		}
		size_t end = pos;
		int read = 0;
		while (read < count && end < text.size()) {
			const size_t newline = text.find('\n', end);
			end = newline == String::npos ? text.size() : newline + 1;
			read++;
		}
		r_result = "[" + path + ", lignes " + std::to_string(first) + " à " + std::to_string(first + read - 1) + " sur " + std::to_string(total) + "]\n" + text.substr(pos, end - pos);
		return true;
	}

	if (p_call.name == "ecrire_fichier" || p_call.name == "remplacer_dans_fichier") {
		String path, error;
		if (!resolve_path(_argument(p_call, "chemin"), path, error)) {
			return fail(error);
		}
		if (path == "res://project.mex") {
			return fail("project.mex ne s'écrit pas directement (l'éditeur le garde en mémoire) : utilise definir_parametre.");
		}
		if (!_is_text_extension(_extension(path))) {
			return fail("Extension refusée pour " + path + " : seuls les fichiers texte (.mx, .scn, .res, .md, .txt, .csv, .cfg, .json, .shader) peuvent être écrits. Pour une image, utilise creer_image.");
		}
		String text;
		if (p_call.name == "ecrire_fichier") {
			if (!p_call.arguments.has(Variant("contenu"))) {
				return fail("Argument « contenu » manquant.");
			}
			text = _argument(p_call, "contenu");
		} else {
			if (!FileAccess::exists(path)) {
				return fail("Le fichier " + path + " n'existe pas.");
			}
			const String before = _argument(p_call, "ancien");
			if (before.empty()) {
				return fail("Argument « ancien » vide.");
			}
			text = FileAccess::get_file_as_string(path);
			const size_t found = text.find(before);
			if (found == String::npos) {
				return fail("Le passage « ancien » est introuvable dans " + path + ". Relis le fichier avec lire_fichier : le texte doit être identique, tabulations et espaces compris.");
			}
			if (text.find(before, found + 1) != String::npos) {
				return fail("Le passage « ancien » apparaît plusieurs fois dans " + path + " : ajoute des lignes voisines pour le rendre unique.");
			}
			text = text.substr(0, found) + _argument(p_call, "nouveau") + text.substr(found + before.size());
		}
		if (text.size() > 400000) {
			return fail("Contenu trop long (" + std::to_string(text.size()) + " caractères, 400000 au plus).");
		}
		if (_extension(path) == "mx" || _extension(path) == "scn") {
			if (!text.empty() && text.back() != '\n') {
				text += '\n';
			}
		}
		const bool existed = FileAccess::exists(path);
		remember(path);
		const size_t slash = path.rfind('/');
		if (slash != String::npos && slash > 5) {
			FileAccess::make_dir_recursive(path.substr(0, slash));
		}
		if (FileAccess::write_string(path, text) != OK) {
			return fail("Impossible d'écrire " + path + ".");
		}
		r_result = String(existed ? "Fichier remplacé : " : "Fichier créé : ") + path + " (" + std::to_string(_count_lines(text)) + " lignes).";
		r_result += check_written(path, text);
		refresh_editor(p_agent, path);
		return true;
	}

	if (p_call.name == "documentation_classe") {
		r_result = _class_documentation(str_strip(_argument(p_call, "classe")));
		return true;
	}

	if (p_call.name == "charger_competence") {
		const String name = str_strip(_argument(p_call, "nom"));
		String available;
		for (const EditorAgent::Skill &skill : p_agent->get_skills()) {
			if (skill.name == name) {
				r_result = skill.body;
				return true;
			}
			available += (available.empty() ? "" : ", ") + skill.name;
		}
		return fail("Compétence inconnue : « " + name + " ». Disponibles : " + available + ".");
	}

	if (p_call.name == "scene_ouverte") {
		if (!editor->scene_root) {
			r_result = "Aucune scène n'est ouverte dans l'éditeur.";
			return true;
		}
		r_result = "Scène : " + (editor->scene_path.empty() ? String("(pas encore enregistrée)") : editor->scene_path) + "\n";
		int count = 0;
		_describe_node(editor->scene_root, 0, r_result, count);
		if (count > 400) {
			r_result += "... (" + std::to_string(count) + " nœuds, liste coupée)\n";
		}
		return true;
	}

	if (p_call.name == "ouvrir_scene") {
		String path, error;
		if (!resolve_path(_argument(p_call, "chemin"), path, error)) {
			return fail(error);
		}
		if (!FileAccess::exists(path)) {
			return fail("La scène " + path + " n'existe pas.");
		}
		if (editor->game.is_running()) {
			editor->_stop();
		}
		if (editor->scene_root && editor->scene_modified && !editor->scene_path.empty()) {
			editor->_save_scene(editor->scene_path);
		}
		if (!editor->_open_scene(path)) {
			return fail("La scène " + path + " ne s'ouvre pas : elle est invalide.");
		}
		r_result = "Scène ouverte dans l'éditeur : " + path;
		return true;
	}

	if (p_call.name == "definir_parametre") {
		const String name = str_strip(_argument(p_call, "nom"));
		if (name.empty() || name.find('/') == String::npos || name.find(' ') != String::npos || name.find('=') != String::npos || name.find('\n') != String::npos || name.find('[') != String::npos) {
			return fail("Nom de paramètre invalide : « " + name + " ». Forme attendue : section/nom, par exemple input/sauter.");
		}
		Variant value = p_call.arguments.get(Variant("valeur"), Variant());
		if (value.get_type() == Variant::NIL) {
			return fail("Argument « valeur » manquant.");
		}
		if (value.get_type() == Variant::STRING) {
			// Texte au format de project.mex ; une chaîne sans guillemets reste une chaîne.
			const String text = str_strip(value.as_string());
			VariantParser::Stream stream(text);
			Variant parsed;
			String error;
			if (!text.empty() && VariantParser::parse_value(stream, parsed, error) == OK && parsed.get_type() != Variant::NIL && parsed.get_type() != Variant::OBJECT) {
				value = parsed;
			} else {
				value = Variant(text);
			}
		}
		// Une action d'entrée est toujours une liste.
		if (str_begins_with(name, "input/") && value.get_type() != Variant::ARRAY) {
			Array list;
			list.push_back(Variant(value.as_string()));
			value = Variant(list);
		}
		// « Annuler les changements » rétablit aussi les paramètres du projet.
		remember(String("res://") + ProjectSettings::PROJECT_FILE);
		ps->set_setting(name, value);
		if (ps->save() != OK) {
			return fail("Impossible d'enregistrer project.mex.");
		}
		editor->_apply_project_settings();
		InputMap::get_singleton()->load_from_project_settings();
		r_result = "Paramètre enregistré : " + name + " = " + VariantWriter::write(value);
		if (name == "application/main_scene" && !FileAccess::exists(value.as_string())) {
			r_result += "\nAttention : le fichier " + value.as_string() + " n'existe pas encore.";
		}
		return true;
	}

	if (p_call.name == "creer_image") {
		String path, error;
		if (!resolve_path(_argument(p_call, "chemin"), path, error)) {
			return fail(error);
		}
		if (_extension(path) != "png") {
			return fail("Le chemin doit se terminer par .png.");
		}
		if (FileAccess::exists(path)) {
			return fail(path + " existe déjà : creer_image ne remplace pas une image. Choisis un autre nom.");
		}
		const int width = (int)p_call.arguments.get(Variant("largeur"), Variant(0)).as_int();
		const int height = (int)p_call.arguments.get(Variant("hauteur"), Variant(0)).as_int();
		if (width < 1 || width > 1024 || height < 1 || height > 1024) {
			return fail("Largeur et hauteur doivent être comprises entre 1 et 1024 pixels.");
		}
		uint8_t color[4];
		if (!_parse_color(_argument(p_call, "couleur"), color)) {
			return fail("Couleur invalide : forme attendue #rrggbb, par exemple #3fa7ff.");
		}
		const String shape = str_to_lower(str_strip(_argument(p_call, "forme")));
		const bool disc = shape == "disque" || shape == "cercle";
		std::vector<uint8_t> pixels((size_t)width * height * 4, 0);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				float alpha = 1.0f;
				if (disc) {
					// Ellipse inscrite, bord adouci sur un pixel.
					const float dx = ((float)x + 0.5f - (float)width * 0.5f) / ((float)width * 0.5f);
					const float dy = ((float)y + 0.5f - (float)height * 0.5f) / ((float)height * 0.5f);
					const float distance = std::sqrt(dx * dx + dy * dy);
					alpha = std::clamp((1.0f - distance) * (float)std::min(width, height) * 0.5f, 0.0f, 1.0f);
				}
				uint8_t *pixel = &pixels[((size_t)y * width + x) * 4];
				pixel[0] = color[0];
				pixel[1] = color[1];
				pixel[2] = color[2];
				pixel[3] = (uint8_t)((float)color[3] * alpha + 0.5f);
			}
		}
		remember(path);
		const size_t slash = path.rfind('/');
		if (slash != String::npos && slash > 5) {
			FileAccess::make_dir_recursive(path.substr(0, slash));
		}
		if (!EditorProjectIcon::write_png(_os_path(path), width, height, pixels.data())) {
			return fail("Impossible d'écrire " + path + ".");
		}
		editor->_scan_filesystem();
		r_result = "Image créée : " + path + " (" + std::to_string(width) + " x " + std::to_string(height) + " pixels).";
		return true;
	}

	if (p_call.name == "lancer_jeu") {
		if (!p_agent->run_started_game) {
			String scene = str_strip(_argument(p_call, "scene"));
			if (scene.empty()) {
				scene = ps->get_setting("application/main_scene").as_string();
				if (scene.empty()) {
					return fail("Le projet n'a pas de scène principale. Règle application/main_scene avec definir_parametre, ou donne l'argument « scene ».");
				}
			} else {
				String error;
				if (!resolve_path(scene, scene, error)) {
					return fail(error);
				}
			}
			if (!FileAccess::exists(scene)) {
				return fail("La scène " + scene + " n'existe pas.");
			}
			double seconds = p_call.arguments.get(Variant("secondes"), Variant(3.0)).as_float();
			if (!(seconds > 0)) {
				seconds = 3.0;
			}
			if (editor->game.is_running()) {
				editor->_stop();
			}
			// « Vider la sortie à chaque lancement » : tout ce qui suit vient de cette exécution.
			p_agent->run_console_start = editor->pref_clear_output_on_run ? 0 : (int)editor->console_lines.size();
			p_agent->run_remaining = std::clamp(seconds, 0.5, 20.0);
			p_agent->run_started_game = true;
			editor->_play(scene);
			if (editor->game.is_running()) {
				return false;
			}
		} else {
			p_agent->run_remaining -= (double)ImGui::GetIO().DeltaTime;
			if (p_agent->run_remaining > 0 && editor->game.is_running()) {
				return false;
			}
		}
		// Fin : on rend la sortie produite pendant l'exécution.
		const bool still_running = editor->game.is_running();
		if (still_running) {
			editor->_stop();
		}
		p_agent->run_started_game = false;
		int errors = 0, warnings = 0;
		String output;
		const size_t start = std::min((size_t)p_agent->run_console_start, editor->console_lines.size());
		for (size_t i = start; i < editor->console_lines.size(); i++) {
			const EditorNode::ConsoleLine &line = editor->console_lines[i];
			if (line.level == PRINT_ERROR) {
				errors++;
				output += "[ERREUR] ";
			} else if (line.level == PRINT_WARNING) {
				warnings++;
				output += "[avertissement] ";
			}
			output += line.text;
			if (line.count > 1) {
				output += "  (x" + std::to_string(line.count) + ")";
			}
			output += "\n";
		}
		r_result = still_running ? "Le jeu a tourné sans s'arrêter de lui-même, puis a été arrêté." : "Le jeu s'est arrêté de lui-même (ou n'a pas pu démarrer).";
		r_result += " " + std::to_string(errors) + " erreur(s), " + std::to_string(warnings) + " avertissement(s).\nSortie :\n" + (output.empty() ? String("(rien)\n") : output);
		if (errors > 0) {
			r_is_error = true;
		}
		return true;
	}

	return fail("Outil inconnu : " + p_call.name + ".");
}
