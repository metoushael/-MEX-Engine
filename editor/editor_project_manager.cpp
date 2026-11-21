// Gestionnaire de projets : la page d'accueil de l'éditeur. Liste des projets connus
// (mémorisée dans les préférences de l'utilisateur), recherche, tri, favoris, création à
// partir d'un modèle, import, scan d'un dossier.
#include "editor_node.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/string/ustring.h"
#include "editor_ui.h"
#include "scene/resources/texture.h"
#include "servers/display_server.h"
#include "servers/rendering_server.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <SDL.h>

#include <algorithm>
#include <ctime>
#include <filesystem>

namespace {

using EditorUI::col;
using EditorUI::u32;

bool _is_project_dir(const String &p_dir) {
	return !p_dir.empty() && FileAccess::exists(path_join(p_dir, ProjectSettings::PROJECT_FILE));
}

// Chemin absolu, simplifié, avec des « / ».
String _absolute(const String &p_path) {
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path abs = fs::absolute(fs::u8path(p_path.empty() ? "." : p_path), ec);
	if (ec) {
		return p_path;
	}
	String result = str_replace(abs.lexically_normal().u8string(), "\\", "/");
	while (result.size() > 1 && result.back() == '/' && !(result.size() == 3 && result[1] == ':')) {
		result.pop_back();
	}
	return result;
}

String _home_dir() {
	for (const char *name : { "HOME", "USERPROFILE" }) {
		if (const char *value = SDL_getenv(name)) {
			if (*value) {
				return _absolute(value);
			}
		}
	}
	return _absolute(".");
}

// « aujourd'hui », « il y a 3 jours »...
String _relative_date(int64_t p_time) {
	if (p_time <= 0) {
		return "jamais ouvert";
	}
	const int64_t elapsed = std::max<int64_t>(0, (int64_t)std::time(nullptr) - p_time);
	if (elapsed < 90) {
		return "à l'instant";
	}
	if (elapsed < 3600) {
		return vformat("il y a %d min", (int)(elapsed / 60));
	}
	if (elapsed < 86400) {
		return vformat("il y a %d h", (int)(elapsed / 3600));
	}
	const int days = (int)(elapsed / 86400);
	if (days == 1) {
		return "hier";
	}
	if (days < 31) {
		return vformat("il y a %d jours", days);
	}
	if (days < 365) {
		return vformat("il y a %d mois", days / 30);
	}
	return days < 730 ? String("il y a 1 an") : vformat("il y a %d ans", days / 365);
}

ImTextureID _tex(const Ref<Texture2D> &p_texture) {
	return (ImTextureID)(intptr_t)RenderingServer::get_singleton()->texture_get_native(p_texture->get_rid());
}

// Texte tronqué par « … » s'il dépasse p_max_width.
void _text_ellipsis(ImDrawList *p_dl, const ImVec2 &p_pos, ImU32 p_color, const String &p_text, float p_max_width) {
	const char *begin = p_text.c_str();
	const char *end = begin + p_text.size();
	if (ImGui::CalcTextSize(begin, end).x <= p_max_width) {
		p_dl->AddText(p_pos, p_color, begin, end);
		return;
	}
	const float dots = ImGui::CalcTextSize("...").x;
	// Les chemins sont coupés par le début : la fin (le nom du dossier) est la partie utile.
	const char *start = begin;
	while (start < end && ImGui::CalcTextSize(start, end).x + dots > p_max_width) {
		start++;
		while (start < end && ((unsigned char)*start & 0xC0) == 0x80) {
			start++; // ne coupe pas un caractère UTF-8 en deux
		}
	}
	p_dl->AddText(p_pos, p_color, "...");
	p_dl->AddText(ImVec2(p_pos.x + dots, p_pos.y), p_color, start, end);
}

} // namespace

// ---------------------------------------------------------------------------
// Liste des projets
// ---------------------------------------------------------------------------
EditorNode::ProjectEntry *EditorNode::_pm_find(const String &p_path) {
	for (ProjectEntry &project : pm_projects) {
		if (project.path == p_path) {
			return &project;
		}
	}
	return nullptr;
}

void EditorNode::_pm_refresh(ProjectEntry &p_project) {
	p_project.missing = !_is_project_dir(p_project.path);
	p_project.name = path_get_file(p_project.path);
	p_project.description.clear();
	p_project.main_scene.clear();
	p_project.icon.unref();
	p_project.icon_loaded = false;
	if (p_project.missing) {
		return;
	}
	ConfigFile config;
	if (config.load(path_join(p_project.path, ProjectSettings::PROJECT_FILE)) != OK) {
		return;
	}
	const String name = config.get_value("application", "name", Variant(String())).as_string();
	if (!name.empty()) {
		p_project.name = name;
	}
	p_project.description = config.get_value("application", "description", Variant(String())).as_string();
	p_project.main_scene = config.get_value("application", "main_scene", Variant(String())).as_string();
	p_project.width = (int)config.get_value("display", "width", Variant(1152)).as_int();
	p_project.height = (int)config.get_value("display", "height", Variant(648)).as_int();
	// Icône du projet (application/icon), chargée directement depuis son dossier.
	const String icon = config.get_value("application", "icon", Variant(String())).as_string();
	if (!icon.empty()) {
		const String file = path_join(p_project.path, str_begins_with(icon, "res://") ? icon.substr(6) : icon);
		if (FileAccess::exists(file)) {
			const std::vector<uint8_t> bytes = FileAccess::get_file_as_bytes(file);
			Ref<Texture2D> texture;
			texture.instantiate();
			if (!bytes.empty() && texture->load_from_memory(bytes.data(), bytes.size()) == OK) {
				p_project.icon = texture;
			}
		}
	}
	p_project.icon_loaded = true;
}

EditorNode::ProjectEntry *EditorNode::_pm_add(const String &p_path, bool p_force) {
	const String path = _absolute(p_path);
	if (ProjectEntry *existing = _pm_find(path)) {
		return existing;
	}
	if (!p_force && std::find(pm_removed.begin(), pm_removed.end(), path) != pm_removed.end()) {
		return nullptr;
	}
	pm_removed.erase(std::remove(pm_removed.begin(), pm_removed.end(), path), pm_removed.end());
	ProjectEntry project;
	project.path = path;
	_pm_refresh(project);
	pm_projects.push_back(project);
	return &pm_projects.back();
}

// Ajoute les projets trouvés dans p_dir et ses sous-dossiers (p_depth niveaux).
int EditorNode::_pm_scan(const String &p_dir, int p_depth) {
	if (!FileAccess::dir_exists(p_dir)) {
		return 0;
	}
	if (_is_project_dir(p_dir)) {
		const size_t before = pm_projects.size();
		_pm_add(p_dir, p_depth >= 100);
		return pm_projects.size() > before ? 1 : 0; // un projet ne contient pas d'autres projets
	}
	if (p_depth % 100 <= 0) {
		return 0;
	}
	int found = 0;
	std::vector<String> dirs;
	FileAccess::list_dir(p_dir, nullptr, &dirs);
	for (const String &dir : dirs) {
		if (dir.empty() || dir[0] == '.' || dir == "build" || dir == "node_modules") {
			continue;
		}
		found += _pm_scan(path_join(p_dir, dir), p_depth - 1);
	}
	return found;
}

void EditorNode::_pm_load_list() {
	pm_projects.clear();
	pm_removed.clear();
	const String file = prefs_dir + "projects.cfg";
	ConfigFile config;
	if (!prefs_dir.empty() && FileAccess::exists(file) && config.load(file) == OK) {
		std::vector<String> sections;
		config.get_sections(&sections);
		for (const String &section : sections) {
			const String path = config.get_value(section, "chemin", Variant(String())).as_string();
			if (path.empty()) {
				continue;
			}
			if (section.rfind("retire", 0) == 0) {
				pm_removed.push_back(path);
				continue;
			}
			if (_pm_find(path)) {
				continue;
			}
			ProjectEntry project;
			project.path = path;
			project.favorite = config.get_value(section, "favori", Variant(false)).as_bool();
			project.last_opened = config.get_value(section, "ouvert", Variant(0)).as_int();
			_pm_refresh(project);
			pm_projects.push_back(project);
		}
		pm_sort = (int)config.get_value("affichage", "tri", Variant(0)).as_int();
	}

	// Projets d'exemple livrés avec le moteur et projets voisins du dossier courant.
	std::vector<String> roots = { ".", "projects", "../projects", "../../projects" };
	if (char *base = SDL_GetBasePath()) {
		roots.push_back(path_join(base, "projects"));
		roots.push_back(path_join(base, "../projects"));
		roots.push_back(path_join(base, "../../projects"));
		SDL_free(base);
	}
	for (const String &root : roots) {
		_pm_scan(root, 1);
		if (pm_new_parent.empty() && FileAccess::dir_exists(root) && path_get_file(_absolute(root)) == "projects") {
			pm_new_parent = _absolute(root);
		}
	}
	if (pm_new_parent.empty()) {
		pm_new_parent = path_join(_home_dir(), "ProjetsMEX");
	}
}

void EditorNode::_pm_save_list() {
	if (prefs_dir.empty()) {
		return;
	}
	ConfigFile config;
	config.set_value("affichage", "tri", Variant(pm_sort));
	int index = 0;
	for (const ProjectEntry &project : pm_projects) {
		const String section = vformat("projet_%03d", index++);
		config.set_value(section, "chemin", Variant(project.path));
		config.set_value(section, "favori", Variant(project.favorite));
		config.set_value(section, "ouvert", Variant((int64_t)project.last_opened));
	}
	index = 0;
	for (const String &path : pm_removed) {
		config.set_value(vformat("retire_%03d", index++), "chemin", Variant(path));
	}
	config.save(prefs_dir + "projects.cfg");
}

// ---------------------------------------------------------------------------
// Création d'un projet
// ---------------------------------------------------------------------------
bool EditorNode::_create_project(const String &p_dir, const String &p_name, int p_template) {
	if (_is_project_dir(p_dir)) {
		pm_error = "Un projet existe déjà dans : " + p_dir;
		return false;
	}
	if (FileAccess::dir_exists(p_dir)) {
		std::vector<String> files, dirs;
		FileAccess::list_dir(p_dir, &files, &dirs);
		if (!files.empty() || !dirs.empty()) {
			pm_error = "Le dossier n'est pas vide : " + p_dir;
			return false;
		}
	} else if (FileAccess::make_dir_recursive(p_dir) != OK) {
		pm_error = "Impossible de créer le dossier : " + p_dir;
		return false;
	}
	String project = "; Projet MEX\n\n[application]\nname = \"" + str_c_escape(p_name) + "\"\n";
	String scene;
	if (p_template == 1) {
		project += "main_scene = \"res://principale.scn\"\n\n[rendering]\nclear_color = Color(0.13, 0.15, 0.24, 1)\n";
		scene = "[mex_scene format=1]\n\n"
				"[node name=\"Principale\" type=\"Node2D\"]\n\n"
				"[node name=\"Camera\" type=\"Camera2D\" parent=\".\"]\n"
				"position = Vector2(576, 324)\n\n"
				"[node name=\"Interface\" type=\"CanvasLayer\" parent=\".\"]\n\n"
				"[node name=\"Titre\" type=\"Label\" parent=\"Interface\"]\n"
				"offset_left = 40.0\noffset_top = 32.0\noffset_right = 900.0\noffset_bottom = 90.0\n"
				"text = \"" + str_c_escape(p_name) + "\"\nfont_size = 40\nfont_color = Color(1, 0.48, 0, 1)\n";
	} else if (p_template == 2) {
		project += "main_scene = \"res://principale.scn\"\n";
		scene = "[mex_scene format=1]\n\n"
				"[sub_resource type=\"Environment\" id=\"1\"]\nbackground_mode = 1\nambient_light_energy = 0.45\n\n"
				"[sub_resource type=\"PlaneMesh\" id=\"2\"]\n\n"
				"[sub_resource type=\"StandardMaterial3D\" id=\"3\"]\nalbedo_color = Color(0.42, 0.58, 0.40, 1)\nroughness = 0.9\n\n"
				"[sub_resource type=\"BoxMesh\" id=\"4\"]\n\n"
				"[sub_resource type=\"StandardMaterial3D\" id=\"5\"]\nalbedo_color = Color(1, 0.48, 0, 1)\nroughness = 0.45\n\n"
				"[node name=\"Principale\" type=\"Node3D\"]\n\n"
				"[node name=\"Environnement\" type=\"WorldEnvironment\" parent=\".\"]\nenvironment = SubResource(\"1\")\n\n"
				"[node name=\"Soleil\" type=\"DirectionalLight3D\" parent=\".\"]\nposition = Vector3(0, 8, 0)\nrotation_degrees = Vector3(-50, 35, 0)\nshadow_enabled = true\n\n"
				"[node name=\"Camera\" type=\"Camera3D\" parent=\".\"]\nposition = Vector3(0, 2.6, 6.5)\nrotation_degrees = Vector3(-18, 0, 0)\ncurrent = true\n\n"
				"[node name=\"Sol\" type=\"MeshInstance3D\" parent=\".\"]\nscale = Vector3(12, 1, 12)\nmesh = SubResource(\"2\")\nmaterial_override = SubResource(\"3\")\n\n"
				"[node name=\"Cube\" type=\"MeshInstance3D\" parent=\".\"]\nposition = Vector3(0, 0.5, 0)\nrotation_degrees = Vector3(0, 30, 0)\nmesh = SubResource(\"4\")\nmaterial_override = SubResource(\"5\")\n";
	}
	if (FileAccess::write_string(path_join(p_dir, ProjectSettings::PROJECT_FILE), project) != OK) {
		pm_error = "Impossible d'écrire le fichier du projet dans : " + p_dir;
		return false;
	}
	if (!scene.empty() && FileAccess::write_string(path_join(p_dir, "principale.scn"), scene) != OK) {
		pm_error = "Impossible d'écrire la scène de départ dans : " + p_dir;
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Sélecteur de dossier
// ---------------------------------------------------------------------------
void EditorNode::_browse_dir(const String &p_title, const String &p_start, std::function<void(const String &)> p_callback) {
	dir_browser.open_request = true;
	dir_browser.title = p_title;
	String start = p_start.empty() ? _home_dir() : _absolute(p_start);
	// Dossier inexistant : on remonte jusqu'au premier parent qui existe.
	for (int i = 0; i < 32 && !FileAccess::dir_exists(start); i++) {
		const String parent = path_get_base_dir(start);
		if (parent.empty() || parent == start) {
			start = _home_dir();
			break;
		}
		start = parent;
	}
	dir_browser.current = start;
	dir_browser.dirty = true;
	dir_browser.callback = std::move(p_callback);
}

void EditorNode::_draw_dir_browser() {
	DirBrowser &b = dir_browser;
	if (b.open_request) {
		b.open_request = false;
		ImGui::OpenPopup("###DirBrowser");
	}
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(std::min(ES(640), viewport->WorkSize.x - ES(40)), std::min(ES(520), viewport->WorkSize.y - ES(40))), ImGuiCond_Appearing);
	bool open = true;
	const String title = String(ICON_FOLDER_OPEN "  ") + b.title + "###DirBrowser";
	if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	if (b.dirty) {
		b.dirty = false;
		b.dirs.clear();
		FileAccess::list_dir(b.current, nullptr, &b.dirs);
		b.dirs.erase(std::remove_if(b.dirs.begin(), b.dirs.end(), [](const String &d) { return d.empty() || d[0] == '.' || d[0] == '$'; }), b.dirs.end());
		std::sort(b.dirs.begin(), b.dirs.end(), [](const String &a, const String &c) { return str_to_lower(a) < str_to_lower(c); });
		b.typed = b.current;
		b.is_project = _is_project_dir(b.current);
	}
	auto go = [&](const String &p_dir) {
		if (FileAccess::dir_exists(p_dir)) {
			b.current = _absolute(p_dir);
			b.dirty = true;
		}
	};

	// --- Barre de navigation ---
	const String parent = path_get_base_dir(b.current);
	ImGui::BeginDisabled(parent.empty() || parent == b.current);
	if (ImGui::Button(ICON_ARROW_UP)) {
		go(parent);
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Dossier parent");
	ImGui::SameLine();
	if (ImGui::Button(ICON_HOME)) {
		go(_home_dir());
	}
	ImGui::SetItemTooltip("Dossier personnel");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputText("##path", &b.typed, ImGuiInputTextFlags_EnterReturnsTrue)) {
		go(b.typed);
	}
#ifdef _WIN32
	// Lecteurs disponibles.
	for (char letter = 'A'; letter <= 'Z'; letter++) {
		const String drive = String(1, letter) + ":/";
		if (FileAccess::dir_exists(drive)) {
			if (ImGui::SmallButton((String(ICON_HARD_DRIVE " ") + String(1, letter) + ":").c_str())) {
				go(drive);
			}
			ImGui::SameLine();
		}
	}
	ImGui::NewLine();
#endif

	// --- Dossiers ---
	const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, col(palette.frame));
	ImGui::BeginChild("##dirs", ImVec2(0, -footer), ImGuiChildFlags_Borders);
	String enter;
	if (b.dirs.empty()) {
		ImGui::TextDisabled("Ce dossier ne contient aucun sous-dossier.");
	}
	for (const String &dir : b.dirs) {
		const String full = path_join(b.current, dir);
		const bool project = _is_project_dir(full);
		ImGui::PushStyleColor(ImGuiCol_Text, col(project ? palette.accent : palette.folder));
		ImGui::TextUnformatted(project ? ICON_ROCKET : ICON_FOLDER);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (ImGui::Selectable(dir.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups)) {
			enter = full;
		}
		if (project) {
			ImGui::SameLine();
			ImGui::TextDisabled("projet MEX");
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
	if (!enter.empty()) {
		go(enter);
	}

	// --- Validation ---
	if (b.is_project) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(col(palette.accent), ICON_ROCKET "  Ce dossier contient un projet MEX");
		ImGui::SameLine();
	}
	const float button_w = ES(150);
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - button_w * 2 - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("Annuler", ImVec2(button_w, 0)) || !open) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (_accent_button("Choisir ce dossier", button_w)) {
		const String chosen = b.current;
		auto callback = b.callback;
		ImGui::CloseCurrentPopup();
		if (callback) {
			_defer([callback, chosen]() { callback(chosen); });
		}
	}
	ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------
void EditorNode::_draw_project_row(ProjectEntry &p_project, float p_width) {
	const ImGuiStyle &style = ImGui::GetStyle();
	const float height = ES(76);
	const bool selected = pm_selected == p_project.path;
	ImGui::PushID(p_project.path.c_str());
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 end(pos.x + p_width, pos.y + height);
	ImDrawList *dl = ImGui::GetWindowDrawList();

	// Zone cliquable de la ligne ; l'étoile, dessinée par-dessus, garde la priorité.
	ImGui::SetNextItemAllowOverlap();
	if (ImGui::InvisibleButton("##row", ImVec2(p_width, height))) {
		pm_selected = p_project.path;
	}
	const bool hovered = ImGui::IsItemHovered();
	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !p_project.missing) {
		const String target = p_project.path;
		_defer([this, target]() { _open_project(target); });
	}
	if (ImGui::BeginPopupContextItem("##ctx")) {
		pm_selected = p_project.path;
		const String target = p_project.path;
		if (ImGui::MenuItem(ICON_EDIT "  Modifier", nullptr, false, !p_project.missing)) {
			_defer([this, target]() { _open_project(target); });
		}
		if (ImGui::MenuItem(ICON_PLAY "  Exécuter", nullptr, false, !p_project.missing && !p_project.main_scene.empty())) {
			_defer([this, target]() {
				pm_play_after_open = true;
				if (!_open_project(target)) {
					pm_play_after_open = false;
				}
			});
		}
		if (ImGui::MenuItem(ICON_COPY "  Copier le chemin")) {
			ImGui::SetClipboardText(p_project.path.c_str());
		}
		ImGui::EndPopup();
	}

	const float rounding = style.FrameRounding;
	if (selected) {
		dl->AddRectFilled(pos, end, u32(palette.accent, palette.dark ? 0.16f : 0.14f), rounding);
		dl->AddRectFilled(pos, ImVec2(pos.x + ES(4), end.y), u32(palette.accent), rounding, ImDrawFlags_RoundCornersLeft);
	} else if (hovered) {
		dl->AddRectFilled(pos, end, u32(palette.bg_light, 0.75f), rounding);
	}

	// --- Étoile (favori) ---
	const float star_w = ES(34);
	ImGui::SetCursorScreenPos(ImVec2(pos.x + ES(8), pos.y + (height - ImGui::GetFrameHeight()) * 0.5f));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Text, col(p_project.favorite ? palette.warning : palette.text_dim, p_project.favorite ? 1.0f : 0.6f));
	if (ImGui::Button(ICON_STAR "##fav", ImVec2(star_w, 0))) {
		p_project.favorite = !p_project.favorite;
		_pm_save_list();
	}
	ImGui::PopStyleColor(2);
	ImGui::SetItemTooltip(p_project.favorite ? "Retirer des favoris" : "Ajouter aux favoris (affiché en tête de liste)");

	// --- Icône ---
	const float icon_size = ES(52);
	const ImVec2 icon_pos(pos.x + ES(8) + star_w + ES(8), pos.y + (height - icon_size) * 0.5f);
	const ImVec2 icon_end(icon_pos.x + icon_size, icon_pos.y + icon_size);
	if (p_project.missing) {
		dl->AddRectFilled(icon_pos, icon_end, u32(palette.frame), ES(10));
		const ImVec2 size = ImGui::CalcTextSize(ICON_WARNING);
		dl->AddText(ImVec2(icon_pos.x + (icon_size - size.x) * 0.5f, icon_pos.y + (icon_size - size.y) * 0.5f), u32(palette.warning), ICON_WARNING);
	} else if (p_project.icon.is_valid()) {
		dl->AddImageRounded(_tex(p_project.icon), icon_pos, icon_end, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, ES(10));
	} else if (logo.is_valid()) {
		dl->AddImageRounded(_tex(logo), icon_pos, icon_end, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, ES(10));
	}

	// --- Informations à droite : dernière ouverture et définition ---
	const String date = _relative_date(p_project.last_opened);
	const String resolution = p_project.missing ? String() : vformat("%d x %d", p_project.width, p_project.height);
	float right_w = std::max(ImGui::CalcTextSize(date.c_str()).x, ImGui::CalcTextSize(resolution.c_str()).x) + ES(18);
	const float line = ImGui::GetTextLineHeight();
	const float text_x = icon_end.x + ES(14);
	if (end.x - right_w - ES(12) - text_x < ES(150)) {
		right_w = ES(6); // liste étroite : le nom et le chemin passent avant la date
	} else {
		dl->AddText(ImVec2(end.x - right_w, pos.y + height * 0.5f - line - ES(1)), u32(palette.text_dim), date.c_str());
		dl->AddText(ImVec2(end.x - right_w, pos.y + height * 0.5f + ES(3)), u32(palette.text_dim, 0.8f), resolution.c_str());
	}

	// --- Nom et chemin ---
	const float text_w = end.x - right_w - ES(12) - text_x;
	const float title_size = style.FontSizeBase * 1.22f;
	ImGui::PushFont(nullptr, title_size);
	const float title_h = ImGui::GetTextLineHeight();
	const float block_h = title_h + ES(4) + line;
	const float text_y = pos.y + (height - block_h) * 0.5f;
	_text_ellipsis(dl, ImVec2(text_x, text_y), u32(p_project.missing ? palette.text_dim : palette.text), p_project.name, text_w);
	ImGui::PopFont();
	const String subtitle = p_project.missing ? "Introuvable : " + p_project.path : p_project.path;
	_text_ellipsis(dl, ImVec2(text_x, text_y + title_h + ES(4)), u32(p_project.missing ? palette.warning : palette.text_dim), subtitle, text_w);
	if (hovered) {
		ImGui::SetItemTooltip("%s", p_project.path.c_str());
	}

	ImGui::SetCursorScreenPos(ImVec2(pos.x, end.y + ES(4)));
	ImGui::Dummy(ImVec2(0, 0));
	ImGui::PopID();
}

void EditorNode::_draw_project_manager() {
	if (!pm_scanned) {
		pm_scanned = true;
		_pm_load_list();
		DisplayServer::get_singleton()->window_set_title("MEX - Gestionnaire de projets");
	}
	const ImGuiStyle &style = ImGui::GetStyle();
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImDrawList *bg = ImGui::GetBackgroundDrawList();

	// --- Bandeau supérieur : la marque, puis les réglages ---
	const float header_h = ES(72);
	bg->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + header_h), u32(palette.bg_dark));
	bg->AddRectFilled(ImVec2(viewport->Pos.x, viewport->Pos.y + header_h), ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), u32(palette.bg));
	bg->AddLine(ImVec2(viewport->Pos.x, viewport->Pos.y + header_h), ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + header_h), u32(palette.border));

	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ES(22), ES(12)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("###ProjectManager", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::PopStyleVar(3);

	const float window_w = ImGui::GetWindowWidth();
	const ImVec2 window_pos = ImGui::GetWindowPos();
	const float brand_end = _draw_brand_header(ES(48)) - window_pos.x;
	// Boutons de la fenêtre (barre de titre intégrée) : dans l'angle, en haut à droite.
	const float window_buttons_w = _get_window_buttons_width();
	const float window_buttons_h = ES(32);
	// Les boutons du bandeau se placent à gauche de ceux de la fenêtre, et se réduisent à
	// leur icône quand le bandeau est étroit.
	const float header_right = window_buttons_w > 0.0f ? window_w - window_buttons_w - ES(14) : window_w - ES(22);
	const bool header_compact = header_right - ES(150) * 2 - style.ItemSpacing.x < brand_end + ES(20);
	const float header_button_w = header_compact ? ImGui::GetFrameHeight() + ES(10) : ES(150);
	const float header_buttons_x = header_right - header_button_w * 2 - style.ItemSpacing.x;
	const float header_buttons_y = (header_h - ImGui::GetFrameHeight()) * 0.5f;
	ImGui::SetCursorPos(ImVec2(header_buttons_x, header_buttons_y));
	if (ImGui::Button(header_compact ? ICON_PALETTE "###appearance" : ICON_PALETTE "  Apparence###appearance", ImVec2(header_button_w, 0))) {
		prefs_open = true;
	}
	ImGui::SetItemTooltip("Apparence : thème, échelle de l'interface, taille du texte, barre de titre");
	ImGui::SameLine();
	if (ImGui::Button(header_compact ? ICON_INFO "###about" : ICON_INFO "  À propos###about", ImVec2(header_button_w, 0))) {
		about_open = true;
	}
	if (header_compact) {
		ImGui::SetItemTooltip("À propos de MEX");
	}
	if (window_buttons_w > 0.0f) {
		_draw_window_buttons(0.0f, window_buttons_h);
		// Le bandeau déplace la fenêtre, sauf sur ses boutons.
		const float x0 = window_pos.x, y0 = window_pos.y;
		const float buttons_end = header_buttons_x + header_button_w * 2 + style.ItemSpacing.x;
		_add_caption_area(x0, y0, x0 + header_buttons_x, y0 + header_h);
		_add_caption_area(x0 + header_buttons_x, y0, x0 + buttons_end, y0 + header_buttons_y);
		_add_caption_area(x0 + header_buttons_x, y0 + header_buttons_y + ImGui::GetFrameHeight(), x0 + buttons_end, y0 + header_h);
		_add_caption_area(x0 + buttons_end, y0, x0 + window_w - window_buttons_w, y0 + header_h);
		_add_caption_area(x0 + window_w - window_buttons_w, y0 + window_buttons_h, x0 + window_w, y0 + header_h);
	}

	// --- Barre d'outils : actions à gauche, recherche et tri à droite ---
	ImGui::SetCursorPos(ImVec2(ES(22), header_h + ES(18)));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ES(14), ES(8)));
	if (_accent_button(ICON_PLUS "  Créer")) {
		pm_dialog = PM_DIALOG_CREATE;
		pm_dialog_open_request = true;
		pm_error.clear();
	}
	ImGui::SetItemTooltip("Créer un nouveau projet");
	ImGui::SameLine();
	if (ImGui::Button(ICON_FOLDER_OPEN "  Importer")) {
		pm_dialog = PM_DIALOG_IMPORT;
		pm_dialog_open_request = true;
		pm_error.clear();
	}
	ImGui::SetItemTooltip("Ajouter à la liste un projet présent sur ce poste");
	ImGui::SameLine();
	if (ImGui::Button(ICON_SCAN "  Scanner")) {
		_browse_dir("Dossier à parcourir à la recherche de projets", pm_new_parent, [this](const String &p_dir) {
			const int found = _pm_scan(p_dir, 103);
			_pm_save_list();
			pm_error = found > 0 ? String() : "Aucun nouveau projet trouvé dans : " + p_dir;
		});
	}
	ImGui::SetItemTooltip("Chercher tous les projets d'un dossier et de ses sous-dossiers");

	// La recherche et le tri s'adaptent à la largeur : le libellé « Trier par » disparaît
	// d'abord, puis les deux passent sur une seconde ligne.
	const char *sorts[] = { "Dernière ouverture", "Nom", "Chemin" };
	const float sort_w = std::min(ES(190), std::max(window_w - ES(44), ES(60)) * 0.45f);
	float sort_label_w = ImGui::CalcTextSize("Trier par").x + style.ItemSpacing.x;
	float search_x = ImGui::GetItemRectMax().x - window_pos.x + ES(24);
	const float sort_x = window_w - ES(22) - sort_w;
	bool toolbar_two_rows = false;
	if (sort_x - sort_label_w - ES(16) - search_x < ES(170)) {
		sort_label_w = 0.0f;
	}
	if (sort_x - sort_label_w - ES(16) - search_x < ES(140)) {
		toolbar_two_rows = true;
		search_x = ES(22);
	}
	const float toolbar_row_h = ImGui::GetFrameHeight() + ES(8);
	if (toolbar_two_rows) {
		ImGui::SetCursorPos(ImVec2(search_x, header_h + ES(18) + toolbar_row_h));
	} else {
		ImGui::SameLine(search_x);
	}
	const float toolbar_y = ImGui::GetCursorPosY();
	ImGui::SetNextItemWidth(std::max(ES(60), sort_x - sort_label_w - ES(16) - search_x));
	ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Filtrer les projets", &pm_filter);
	if (sort_label_w > 0.0f) {
		ImGui::SetCursorPos(ImVec2(sort_x - sort_label_w, toolbar_y));
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Trier par");
	}
	ImGui::SetCursorPos(ImVec2(sort_x, toolbar_y));
	ImGui::SetNextItemWidth(sort_w);
	if (ImGui::BeginCombo("##sort", sorts[std::clamp(pm_sort, 0, 2)])) {
		for (int i = 0; i < 3; i++) {
			if (ImGui::Selectable(sorts[i], pm_sort == i)) {
				pm_sort = i;
				_pm_save_list();
			}
		}
		ImGui::EndCombo();
	}
	if (sort_label_w <= 0.0f) {
		ImGui::SetItemTooltip("Trier par");
	}
	ImGui::PopStyleVar();

	// --- Liste filtrée et triée ---
	const String filter = str_to_lower(str_strip(pm_filter));
	std::vector<int> order;
	for (int i = 0; i < (int)pm_projects.size(); i++) {
		const ProjectEntry &project = pm_projects[i];
		if (filter.empty() || str_to_lower(project.name).find(filter) != String::npos || str_to_lower(project.path).find(filter) != String::npos) {
			order.push_back(i);
		}
	}
	std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
		const ProjectEntry &pa = pm_projects[a], &pb = pm_projects[b];
		if (pa.favorite != pb.favorite) {
			return pa.favorite; // les favoris d'abord
		}
		if (pm_sort == 0 && pa.last_opened != pb.last_opened) {
			return pa.last_opened > pb.last_opened;
		}
		if (pm_sort == 2) {
			return str_to_lower(pa.path) < str_to_lower(pb.path);
		}
		return str_to_lower(pa.name) < str_to_lower(pb.name);
	});
	ProjectEntry *selected = _pm_find(pm_selected);
	if (!selected && !order.empty() && filter.empty()) {
		pm_selected = pm_projects[order[0]].path;
		selected = &pm_projects[order[0]];
	}

	const float footer_h = ImGui::GetTextLineHeight() + ES(14);
	const float top = header_h + ES(18) + ImGui::GetFrameHeight() + ES(6) * 2 + ES(18) + (toolbar_two_rows ? toolbar_row_h : 0.0f);
	const float list_h = std::max(ImGui::GetWindowHeight() - top - footer_h - ES(12), ES(60));
	// Colonne d'actions : avec les noms, ou réduite aux icônes quand la liste manquerait de place.
	const bool side_compact = window_w - ES(22) * 2 - ES(210) - ES(16) < ES(380);
	const float side_w = side_compact ? ImGui::GetFrameHeight() + ES(22) : ES(210);
	const float list_w = std::max(window_w - ES(22) * 2 - side_w - ES(16), ES(60));
	ImGui::SetCursorPos(ImVec2(ES(22), top));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, col(palette.frame));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ES(8), ES(8)));
	ImGui::BeginChild("##projects", ImVec2(list_w, list_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();
	if (order.empty()) {
		// --- Liste vide ---
		const char *title = pm_projects.empty() ? "Aucun projet pour l'instant" : "Aucun projet ne correspond au filtre";
		const char *hint = pm_projects.empty() ? "Créez votre premier projet, ou importez un dossier contenant un fichier project.mex." : "Modifiez ou effacez le texte du champ de recherche.";
		const float avail_w = ImGui::GetContentRegionAvail().x;
		ImGui::Dummy(ImVec2(0, std::max(ES(20), list_h * 0.28f)));
		ImGui::PushFont(nullptr, style.FontSizeBase * 1.5f);
		// Centrés ; dans une liste étroite, les textes reviennent à la ligne au lieu de déborder.
		ImGui::PushTextWrapPos(ES(8) + avail_w);
		ImGui::SetCursorPosX(std::max((avail_w - ImGui::CalcTextSize(title).x) * 0.5f, 0.0f) + ES(8));
		ImGui::TextUnformatted(title);
		ImGui::PopFont();
		ImGui::SetCursorPosX(std::max((avail_w - ImGui::CalcTextSize(hint).x) * 0.5f, 0.0f) + ES(8));
		ImGui::TextDisabled("%s", hint);
		ImGui::PopTextWrapPos();
		if (pm_projects.empty()) {
			ImGui::Dummy(ImVec2(0, ES(10)));
			ImGui::SetCursorPosX(std::max((avail_w - ES(220)) * 0.5f, 0.0f) + ES(8));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ES(14), ES(9)));
			if (_accent_button(ICON_PLUS "  Créer un projet", std::min(ES(220), avail_w))) {
				pm_dialog = PM_DIALOG_CREATE;
				pm_dialog_open_request = true;
				pm_error.clear();
			}
			ImGui::PopStyleVar();
		}
	} else {
		const float row_w = ImGui::GetContentRegionAvail().x;
		for (int index : order) {
			_draw_project_row(pm_projects[index], row_w);
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();

	// --- Colonne d'actions sur le projet sélectionné ---
	selected = _pm_find(pm_selected);
	const bool usable = selected && !selected->missing;
	ImGui::SetCursorPos(ImVec2(ES(22) + list_w + ES(16), top));
	ImGui::BeginChild("##actions", ImVec2(side_w, list_h));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(side_compact ? ES(4) : ES(14), ES(9)));
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(side_compact ? 0.5f : 0.0f, 0.5f));
	ImGui::BeginDisabled(!usable);
	if (_accent_button(side_compact ? ICON_EDIT "###edit" : ICON_EDIT "  Modifier###edit", side_w)) {
		const String target = selected->path;
		_defer([this, target]() { _open_project(target); });
	}
	ImGui::SetItemTooltip("Modifier : ouvrir le projet dans l'éditeur (double-clic sur la ligne)");
	ImGui::EndDisabled();
	ImGui::BeginDisabled(!usable || selected->main_scene.empty());
	if (ImGui::Button(side_compact ? ICON_PLAY "###run" : ICON_PLAY "  Exécuter###run", ImVec2(side_w, 0))) {
		const String target = selected->path;
		_defer([this, target]() {
			pm_play_after_open = true;
			if (!_open_project(target)) {
				pm_play_after_open = false;
			}
		});
	}
	ImGui::SetItemTooltip("Exécuter : ouvrir le projet et lancer sa scène principale");
	ImGui::EndDisabled();
	ImGui::BeginDisabled(!usable);
	if (ImGui::Button(side_compact ? ICON_RENAME "###rename" : ICON_RENAME "  Renommer###rename", ImVec2(side_w, 0))) {
		pm_dialog = PM_DIALOG_RENAME;
		pm_dialog_open_request = true;
		pm_new_name = selected->name;
		pm_error.clear();
	}
	if (side_compact) {
		ImGui::SetItemTooltip("Renommer le projet");
	}
	if (ImGui::Button(side_compact ? ICON_EXTERNAL "###folder" : ICON_EXTERNAL "  Afficher le dossier###folder", ImVec2(side_w, 0))) {
		const String url = "file://" + String(selected->path[0] == '/' ? "" : "/") + selected->path;
		if (SDL_OpenURL(url.c_str()) != 0) {
			pm_error = "Impossible d'ouvrir le dossier : " + selected->path;
		}
	}
	if (side_compact) {
		ImGui::SetItemTooltip("Afficher le dossier du projet");
	}
	ImGui::EndDisabled();
	ImGui::BeginDisabled(!selected);
	const bool remove_pressed = ImGui::Button(side_compact ? ICON_MINUS "###remove" : ICON_MINUS "  Retirer de la liste###remove", ImVec2(side_w, 0));
	if (side_compact) {
		ImGui::SetItemTooltip("Retirer le projet de la liste (ses fichiers sont conservés)");
	}
	if (remove_pressed) {
		const String target = selected->path;
		_confirm("Retirer le projet de la liste", "« " + selected->name + " » ne sera plus affiché dans la liste.\n\nSes fichiers ne sont pas supprimés :\n" + target, {
																																								   { "Retirer", [this, target]() {
																																										pm_projects.erase(std::remove_if(pm_projects.begin(), pm_projects.end(), [&](const ProjectEntry &p) { return p.path == target; }), pm_projects.end());
																																										pm_removed.push_back(target);
																																										pm_selected.clear();
																																										_pm_save_list();
																																									} },
																																								   { "Annuler", nullptr },
																																						   });
	}
	ImGui::EndDisabled();
	ImGui::PopStyleVar(2);

	// Fiche du projet sélectionné.
	if (selected && !side_compact) {
		ImGui::Dummy(ImVec2(0, ES(10)));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, ES(6)));
		ImGui::PushTextWrapPos(side_w);
		ImGui::TextUnformatted(selected->name.c_str());
		if (!selected->description.empty()) {
			ImGui::TextDisabled("%s", selected->description.c_str());
		}
		if (selected->missing) {
			ImGui::TextColored(col(palette.warning), "Le dossier du projet est introuvable (déplacé, supprimé ou sur un disque débranché).");
		} else {
			ImGui::TextDisabled("Scène principale : %s", selected->main_scene.empty() ? "aucune" : selected->main_scene.c_str());
			ImGui::TextDisabled("Définition : %d x %d", selected->width, selected->height);
		}
		ImGui::PopTextWrapPos();
	}
	ImGui::EndChild();

	// --- Pied de page ---
	ImGui::SetCursorPos(ImVec2(ES(22), ImGui::GetWindowHeight() - footer_h - ES(2)));
	if (!pm_error.empty() && pm_dialog == PM_DIALOG_NONE) {
		ImGui::TextColored(col(palette.error), ICON_WARNING "  %s", pm_error.c_str());
	} else {
		const int count = (int)pm_projects.size();
		ImGui::TextDisabled("%d projet%s  ·  MEX %s", count, count > 1 ? "s" : "", MEX_VERSION);
	}
	ImGui::End();

	_draw_project_dialogs();
	_draw_dir_browser();
}

void EditorNode::_draw_project_dialogs() {
	if (pm_dialog_open_request) {
		pm_dialog_open_request = false;
		ImGui::OpenPopup("###PmDialog");
	}
	if (pm_dialog == PM_DIALOG_NONE) {
		return;
	}
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const ImGuiStyle &style = ImGui::GetStyle();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(std::min(ES(620), viewport->WorkSize.x - ES(40)), 0));
	const char *titles[] = { "", ICON_PLUS "  Créer un projet###PmDialog", ICON_FOLDER_OPEN "  Importer un projet###PmDialog", ICON_RENAME "  Renommer le projet###PmDialog" };
	bool open = true;
	if (!ImGui::BeginPopupModal(titles[pm_dialog], &open, ImGuiWindowFlags_NoSavedSettings)) {
		if (!ImGui::IsPopupOpen("###PmDialog")) {
			pm_dialog = PM_DIALOG_NONE;
		}
		return;
	}
	const float label_w = ES(140);
	const float button_w = ES(140);
	const float browse_w = ES(120);
	auto row = [&](const char *p_label) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(p_label);
		ImGui::SameLine(label_w);
	};
	bool confirm = false;
	bool close = !open;
	const char *confirm_label = "Valider";

	if (pm_dialog == PM_DIALOG_CREATE) {
		confirm_label = "Créer et modifier";
		row("Nom du projet");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		confirm |= ImGui::InputText("##name", &pm_new_name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		row("Emplacement");
		ImGui::SetNextItemWidth(-browse_w - style.ItemSpacing.x);
		ImGui::InputText("##parent", &pm_new_parent);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FOLDER_OPEN "  Parcourir", ImVec2(browse_w, 0))) {
			_browse_dir("Dossier où créer le projet", pm_new_parent, [this](const String &p_dir) { pm_new_parent = p_dir; });
		}
		const String name = str_strip(pm_new_name);
		row("");
		ImGui::TextDisabled("Le projet sera créé dans : %s", name.empty() ? "..." : path_join(pm_new_parent, name).c_str());

		// Modèles de départ : trois cartes.
		ImGui::Dummy(ImVec2(0, ES(4)));
		ImGui::TextUnformatted("Modèle de départ");
		struct Template {
			const char *icon, *title, *text;
		};
		const Template templates[3] = {
			{ ICON_FILE, "Projet vide", "Aucune scène : vous partez d'une page blanche." },
			{ ICON_VIEW_2D, "Jeu 2D", "Une scène 2D avec une caméra et un titre." },
			{ ICON_VIEW_3D, "Jeu 3D", "Un sol, un cube, une caméra, un soleil et un ciel." },
		};
		const float card_w = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 2) / 3.0f;
		const float card_h = ES(112);
		for (int i = 0; i < 3; i++) {
			if (i > 0) {
				ImGui::SameLine();
			}
			ImGui::PushID(i);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##template", ImVec2(card_w, card_h))) {
				pm_template = i;
			}
			const bool hovered = ImGui::IsItemHovered();
			const bool current = pm_template == i;
			ImDrawList *dl = ImGui::GetWindowDrawList();
			const ImVec2 end(pos.x + card_w, pos.y + card_h);
			dl->AddRectFilled(pos, end, current ? u32(palette.accent, 0.16f) : u32(hovered ? palette.bg_light : palette.frame), ES(8));
			dl->AddRect(pos, end, u32(current ? palette.accent : palette.border), ES(8), 0, current ? ES(2) : 1.0f);
			ImGui::PushFont(nullptr, style.FontSizeBase * 1.7f);
			dl->AddText(ImVec2(pos.x + ES(12), pos.y + ES(10)), u32(current ? palette.accent : palette.text_dim), templates[i].icon);
			ImGui::PopFont();
			dl->AddText(ImVec2(pos.x + ES(12), pos.y + ES(46)), u32(palette.text), templates[i].title);
			ImGui::PushFont(nullptr, style.FontSizeBase * 0.86f);
			dl->AddText(nullptr, 0.0f, ImVec2(pos.x + ES(12), pos.y + ES(68)), u32(palette.text_dim), templates[i].text, nullptr, card_w - ES(24));
			ImGui::PopFont();
			ImGui::PopID();
		}
	} else if (pm_dialog == PM_DIALOG_IMPORT) {
		confirm_label = "Importer et modifier";
		ImGui::TextWrapped("Indiquez le dossier du projet (celui qui contient le fichier %s).", ProjectSettings::PROJECT_FILE);
		ImGui::Dummy(ImVec2(0, ES(2)));
		row("Dossier du projet");
		ImGui::SetNextItemWidth(-browse_w - style.ItemSpacing.x);
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		confirm |= ImGui::InputText("##open_path", &pm_open_path, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FOLDER_OPEN "  Parcourir", ImVec2(browse_w, 0))) {
			_browse_dir("Dossier du projet à importer", pm_open_path.empty() ? pm_new_parent : pm_open_path, [this](const String &p_dir) { pm_open_path = p_dir; });
		}
	} else if (pm_dialog == PM_DIALOG_RENAME) {
		confirm_label = "Renommer";
		row("Nouveau nom");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		confirm |= ImGui::InputText("##rename", &pm_new_name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::TextDisabled("Seul le nom affiché change : le dossier du projet garde le sien.");
	}

	if (!pm_error.empty()) {
		ImGui::Dummy(ImVec2(0, ES(2)));
		ImGui::PushStyleColor(ImGuiCol_Text, col(palette.error));
		ImGui::TextWrapped(ICON_WARNING "  %s", pm_error.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Dummy(ImVec2(0, ES(6)));
	const float confirm_w = std::max(button_w, ImGui::CalcTextSize(confirm_label).x + style.FramePadding.x * 2 + ES(16));
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - button_w - confirm_w - style.ItemSpacing.x - style.WindowPadding.x);
	if (ImGui::Button("Annuler", ImVec2(button_w, 0))) {
		close = true;
	}
	ImGui::SameLine();
	confirm |= _accent_button(confirm_label, confirm_w);

	if (confirm) {
		if (pm_dialog == PM_DIALOG_CREATE) {
			const String name = str_strip(pm_new_name);
			if (name.empty()) {
				pm_error = "Le nom du projet est vide.";
			} else if (name.find_first_of("/\\:*?\"<>|") != String::npos) {
				pm_error = "Le nom ne peut pas contenir les caractères / \\ : * ? \" < > |";
			} else {
				const String dir = _absolute(path_join(pm_new_parent, name));
				const int kind = pm_template;
				pm_error.clear();
				if (_create_project(dir, name, kind)) {
					close = true;
					_defer([this, dir]() { _open_project(dir); });
				}
			}
		} else if (pm_dialog == PM_DIALOG_IMPORT) {
			String dir = str_strip(pm_open_path);
			if (path_get_file(dir) == ProjectSettings::PROJECT_FILE) {
				dir = path_get_base_dir(dir); // le fichier project.mex lui-même a été indiqué
			}
			if (dir.empty()) {
				pm_error = "Indiquez le dossier du projet.";
			} else if (!_is_project_dir(dir)) {
				pm_error = "Aucun fichier " + String(ProjectSettings::PROJECT_FILE) + " dans : " + dir;
			} else {
				pm_error.clear();
				close = true;
				dir = _absolute(dir);
				_defer([this, dir]() { _open_project(dir); });
			}
		} else if (pm_dialog == PM_DIALOG_RENAME) {
			const String name = str_strip(pm_new_name);
			ProjectEntry *project = _pm_find(pm_selected);
			if (name.empty()) {
				pm_error = "Le nom du projet est vide.";
			} else if (project) {
				// Seule la clé application/name est réécrite ; le reste du fichier est conservé.
				const String file = path_join(project->path, ProjectSettings::PROJECT_FILE);
				ConfigFile config;
				if (config.load(file) == OK) {
					config.set_value("application", "name", Variant(name));
					if (config.save(file) == OK) {
						_pm_refresh(*project);
						pm_error.clear();
						close = true;
					} else {
						pm_error = "Impossible d'écrire : " + file;
					}
				} else {
					pm_error = "Impossible de lire : " + file;
				}
			}
		}
	}
	if (close) {
		pm_dialog = PM_DIALOG_NONE;
		ImGui::CloseCurrentPopup();
	}
	// Le sélecteur de dossier s'ouvre par-dessus cette fenêtre : il doit être dessiné ici,
	// dans la même pile de fenêtres modales.
	_draw_dir_browser();
	ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Ouverture et fermeture d'un projet
// ---------------------------------------------------------------------------
bool EditorNode::_open_project(const String &p_path) {
	if (p_path.empty() || !_is_project_dir(p_path)) {
		pm_error = "Aucun fichier " + String(ProjectSettings::PROJECT_FILE) + " dans : " + p_path;
		return false;
	}
	if (ProjectSettings::get_singleton()->setup(p_path) != OK) {
		pm_error = "Impossible de charger le projet : " + p_path;
		return false;
	}
	pm_error.clear();
	project_loaded = true;
	_project_opened();
	return true;
}
