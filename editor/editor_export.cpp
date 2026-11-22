// Export : produit un jeu autonome (exécutable + archive de données .pck).
#include "editor_node.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/pack.h"
#include "core/os/os.h"
#include "core/string/ustring.h"

<<<<<<< HEAD
#include "editor_export.h"
=======
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
#include "editor_ui.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>

namespace fs = std::filesystem;

<<<<<<< HEAD
// Fenêtre « Exporter le jeu » : la liste des préréglages à gauche, les réglages de celui qui
// est choisi à droite (même disposition que la fenêtre d'export de Godot). Les préréglages
// sont enregistrés dans export_presets.cfg dès qu'ils changent.
void EditorNode::_draw_export_dialog() {
	if (!export_open) {
		export_loaded = false;
		return;
	}
	const String project_dir = ProjectSettings::get_singleton()->get_resource_path();
	const String game_name = GLOBAL_GET("application/name").as_string();
	if (!export_loaded) {
		export_presets = EditorExport::load_presets(project_dir);
		export_loaded = true;
		export_selected = export_presets.empty() ? -1 : std::min(std::max(export_selected, 0), (int)export_presets.size() - 1);
	}
	bool changed = false;
	const auto run_export = [&](const std::vector<int> &p_indices) {
		if (scene_root && scene_modified && !scene_path.empty()) {
			_save_scene(scene_path);
		}
		if (script_dirty) {
			_save_script();
		}
		export_message.clear();
		export_failed = false;
		for (int index : p_indices) {
			String message;
			const bool ok = EditorExport::export_preset(project_dir, export_presets[index], message);
			if (ok) {
				print_line(message);
			} else {
				print_error("Export « " + export_presets[index].name + " » : " + message);
				export_failed = true;
			}
			export_message += (export_message.empty() ? "" : "\n") + message;
		}
		fs_dirty = true;
	};

	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(std::min(ES(820), viewport->WorkSize.x - ES(40)), std::min(ES(560), viewport->WorkSize.y - ES(40))), ImGuiCond_Appearing);
	if (ImGui::Begin("Exporter le jeu###Export", &export_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
		const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ES(70);
		// --- Liste des préréglages ---
		ImGui::BeginChild("##presets", ImVec2(ES(230), -footer), ImGuiChildFlags_Borders);
		ImGui::TextDisabled("Préréglages");
		for (int i = 0; i < (int)export_presets.size(); i++) {
			ImGui::PushID(i);
			if (ImGui::Selectable(export_presets[i].name.empty() ? "(sans nom)" : export_presets[i].name.c_str(), export_selected == i)) {
				export_selected = i;
			}
			ImGui::PopID();
		}
		ImGui::Spacing();
		if (ImGui::Button(ICON_PLUS "  Ajouter...", ImVec2(-FLT_MIN, 0))) {
			ImGui::OpenPopup("##add_preset");
		}
		if (ImGui::BeginPopup("##add_preset")) {
			for (const String &platform : EditorExport::get_platforms()) {
				if (ImGui::MenuItem(platform.c_str())) {
					ExportPreset preset = EditorExport::make_preset(platform, game_name);
					// Nom unique : « Linux », « Linux 2 »...
					int suffix = 1;
					const String base = preset.name;
					while (std::any_of(export_presets.begin(), export_presets.end(), [&](const ExportPreset &p) { return p.name == preset.name; })) {
						preset.name = base + " " + itos(++suffix);
					}
					export_presets.push_back(preset);
					export_selected = (int)export_presets.size() - 1;
					changed = true;
				}
			}
			ImGui::EndPopup();
		}
		ImGui::BeginDisabled(export_selected < 0);
		if (ImGui::Button("Supprimer", ImVec2(-FLT_MIN, 0)) && export_selected >= 0) {
			export_presets.erase(export_presets.begin() + export_selected);
			export_selected = std::min(export_selected, (int)export_presets.size() - 1);
			changed = true;
		}
		ImGui::EndDisabled();
		ImGui::EndChild();

		// --- Réglages du préréglage choisi ---
		ImGui::SameLine();
		ImGui::BeginChild("##options", ImVec2(0, -footer), ImGuiChildFlags_Borders);
		if (export_selected < 0 || export_selected >= (int)export_presets.size()) {
			ImGui::TextWrapped("Un préréglage décrit un export : système visé, fichier à produire, fichiers à inclure. Ajoutez-en un par système (Windows, Linux, macOS), puis « Exporter » ou « Tout exporter ».");
		} else {
			ExportPreset &preset = export_presets[export_selected];
			const bool macos = preset.platform == "macOS";
			const auto field = [&](const char *p_label, const char *p_id, String &r_value, const char *p_hint = nullptr) {
				ImGui::TextUnformatted(p_label);
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (p_hint) {
					ImGui::InputTextWithHint(p_id, p_hint, &r_value);
				} else {
					ImGui::InputText(p_id, &r_value);
				}
				changed |= ImGui::IsItemDeactivatedAfterEdit();
			};
			field("Nom", "##name", preset.name);
			ImGui::TextDisabled("Système : %s", preset.platform.c_str());
			field(macos ? "Paquet à produire (.app)" : "Exécutable à produire", "##path", preset.export_path);
			ImGui::TextDisabled("Relatif au dossier du projet, ou chemin complet.");
			const String found = EditorExport::find_template(preset.platform);
			field("Modèle personnalisé (exécutable mex-jeu du système visé)", "##template", preset.custom_template, found.empty() ? "introuvable : voir ci-dessous" : found.c_str());
			if (preset.custom_template.empty() && found.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, EditorUI::col(palette.warning));
				ImGui::TextWrapped("Modèle introuvable. Copiez le mex-jeu de ce système ici :\n%s", EditorExport::get_template_hint(preset.platform).c_str());
				ImGui::PopStyleColor();
			}
			ImGui::Spacing();
			if (macos) {
				field("Identifiant du paquet", "##bundle", preset.bundle_identifier, "com.exemple.jeu");
				ImGui::TextDisabled("Le paquet n'est pas signé : au premier lancement, clic droit > Ouvrir.");
			} else {
				changed |= ImGui::Checkbox("Incorporer l'archive à l'exécutable", &preset.embed_pck);
				ImGui::SetItemTooltip("Cochée : un seul fichier à distribuer.\nDécochée : l'exécutable et une archive « .pck » du même nom, à garder ensemble.");
			}
			ImGui::Spacing();
			field("Fichiers à exclure", "##exclude", preset.exclude_filter, "*.psd, brouillons/*");
			field("Fichiers à inclure quand même", "##include", preset.include_filter, ".notes/*.txt");
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("Tout le projet est exporté, sauf les fichiers et dossiers cachés, le dossier « export », les archives .pck et les dossiers qui contiennent un fichier « .mexignore ». Listes séparées par des virgules ; « * » remplace n'importe quelle suite de caractères.");
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();

		// --- Actions ---
		ImGui::BeginDisabled(export_selected < 0);
		if (_accent_button("Exporter", ES(140)) && export_selected >= 0) {
			run_export({ export_selected });
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(export_presets.empty());
		if (ImGui::Button("Tout exporter", ImVec2(ES(140), 0))) {
			std::vector<int> all;
			for (int i = 0; i < (int)export_presets.size(); i++) {
				all.push_back(i);
			}
			run_export(all);
		}
		ImGui::EndDisabled();
=======
namespace {

String _generic(const fs::path &p_path) {
	return str_replace(p_path.lexically_normal().u8string(), "\\", "/");
}

// Nom de fichier sûr à partir du nom du jeu.
String _file_name(const String &p_name) {
	String out;
	for (unsigned char c : p_name) {
		if (std::isalnum(c) || c == '-' || c == '_' || c >= 0x80) {
			out += (char)c;
		} else if (c == ' ' && !out.empty() && out.back() != '_') {
			out += '_';
		}
	}
	while (!out.empty() && out.back() == '_') {
		out.pop_back();
	}
	return out.empty() ? String("jeu") : out;
}

} // namespace

bool EditorNode::_export_project(const String &p_dir, const String &p_template, String &r_message) {
	std::error_code ec;
	const fs::path project_dir = fs::u8path(ProjectSettings::get_singleton()->get_resource_path());
	fs::path out_dir = fs::u8path(p_dir);
	if (out_dir.is_relative()) {
		out_dir = project_dir / out_dir;
	}
	out_dir = out_dir.lexically_normal();
	const fs::path template_path = fs::u8path(p_template);
	if (p_dir.empty()) {
		r_message = "Indiquez un dossier de destination.";
		return false;
	}
	if (!fs::is_regular_file(template_path, ec)) {
		r_message = "Exécutable introuvable : " + p_template;
		return false;
	}
	if (fs::equivalent(out_dir, project_dir, ec)) {
		r_message = "Choisissez un autre dossier que celui du projet (un sous-dossier convient).";
		return false;
	}
	ec.clear();
	fs::create_directories(out_dir, ec);
	if (ec) {
		r_message = "Impossible de créer le dossier : " + _generic(out_dir);
		return false;
	}

	// Fichiers du projet (sans les fichiers cachés ni le dossier d'export lui-même).
	std::vector<std::pair<String, String>> files;
	uint64_t total_size = 0;
	for (fs::recursive_directory_iterator it(project_dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
		const fs::path &path = it->path();
		const String name = path.filename().u8string();
		std::error_code ec2;
		if (it->is_directory(ec2)) {
			if ((!name.empty() && name[0] == '.') || fs::equivalent(path, out_dir, ec2)) {
				it.disable_recursion_pending();
			}
			continue;
		}
		if (name.empty() || name[0] == '.' || !it->is_regular_file(ec2) || path_get_extension(name) == "pck") {
			continue;
		}
		files.push_back({ _generic(fs::relative(path, project_dir, ec2)), _generic(path) });
		total_size += (uint64_t)it->file_size(ec2);
	}
	std::sort(files.begin(), files.end());

	const String name = _file_name(GLOBAL_GET("application/name").as_string());
	const fs::path pack_path = out_dir / fs::u8path(name + ".pck");
	const Error err = PackedData::create(_generic(pack_path), files);
	if (err != OK) {
		r_message = "Échec de l'écriture de l'archive : " + _generic(pack_path);
		return false;
	}

	// L'exécutable porte le nom du jeu : au lancement, il charge l'archive du même nom.
	const String extension = template_path.extension().u8string(); // « .exe » sous Windows
	const fs::path exe_path = out_dir / fs::u8path(name + extension);
	if (FileAccess::copy(_generic(template_path), _generic(exe_path)) != OK) {
		r_message = "Impossible de copier l'exécutable vers : " + _generic(exe_path);
		return false;
	}
	// Bibliothèques placées à côté de l'exécutable (SDL2.dll sous Windows).
	int libraries = 0;
	for (fs::directory_iterator it(template_path.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
		const String ext = path_get_extension(it->path().filename().u8string());
		if (ext == "dll" || ext == "dylib") {
			if (FileAccess::copy(_generic(it->path()), _generic(out_dir / it->path().filename())) == OK) {
				libraries++;
			}
		}
	}

	r_message = "Jeu exporté dans " + _generic(out_dir) + "\n  " + name + extension + "  (exécutable)\n  " + name + ".pck  (" + itos((int64_t)files.size()) + " fichiers, " + itos((int64_t)((total_size + 1023) / 1024)) + " Ko)";
	if (libraries > 0) {
		r_message += "\n  " + itos(libraries) + " bibliothèque(s)";
	}
	return true;
}

void EditorNode::_draw_export_dialog() {
	if (!export_open) {
		return;
	}
	if (export_dir.empty()) {
		export_dir = path_join(ProjectSettings::get_singleton()->get_resource_path(), "export");
	}
	if (export_template.empty()) {
		// De préférence « mex-jeu », l'exécutable sans éditeur fourni à côté de l'éditeur.
		export_template = OS::get_singleton()->get_executable_path();
		std::error_code ec;
		const fs::path editor_path = fs::u8path(export_template);
		const fs::path game_path = editor_path.parent_path() / fs::u8path("mex-jeu" + editor_path.extension().u8string());
		if (!export_template.empty() && fs::is_regular_file(game_path, ec)) {
			export_template = _generic(game_path);
		}
	}
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	const float export_w = std::min(ES(640), viewport->WorkSize.x - ES(40));
	ImGui::SetNextWindowSizeConstraints(ImVec2(export_w, 0), ImVec2(export_w, FLT_MAX));
	if (ImGui::Begin("Exporter le jeu###Export", &export_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("Crée un dossier contenant l'exécutable du jeu et une archive « .pck » avec tous les fichiers du projet. Ce dossier peut être copié ou compressé tel quel pour distribuer le jeu.");
		ImGui::Spacing();
		ImGui::TextUnformatted("Dossier de destination");
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputText("##dir", &export_dir);
		ImGui::TextUnformatted("Exécutable du moteur à utiliser");
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputText("##template", &export_template);
		ImGui::TextDisabled("Par défaut : « mex-jeu », l'exécutable sans éditeur fourni à côté de l'éditeur (sinon l'éditeur\nlui-même). Pour un autre système, indiquez le mex-jeu de ce système (Windows, Linux, macOS).");
		ImGui::Spacing();
		if (ImGui::Button("Exporter", ImVec2(ES(120), 0))) {
			if (scene_root && scene_modified && !scene_path.empty()) {
				_save_scene(scene_path);
			}
			if (script_dirty) {
				_save_script();
			}
			export_failed = !_export_project(str_strip(export_dir), str_strip(export_template), export_message);
			if (export_failed) {
				print_error("Export : " + export_message);
			} else {
				print_line(export_message);
				fs_dirty = true;
			}
		}
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
		ImGui::SameLine();
		if (ImGui::Button("Fermer", ImVec2(ES(120), 0))) {
			export_open = false;
		}
		if (!export_message.empty()) {
<<<<<<< HEAD
			ImGui::BeginChild("##message", ImVec2(0, 0));
			ImGui::PushStyleColor(ImGuiCol_Text, EditorUI::col(export_failed ? palette.error : palette.success));
			ImGui::TextWrapped("%s", export_message.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();
		}
	}
	ImGui::End();
	if (changed) {
		if (EditorExport::save_presets(project_dir, export_presets) != OK) {
			print_error("Impossible d'enregistrer " + String(EditorExport::PRESETS_FILE));
		}
		fs_dirty = true;
	}
=======
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, EditorUI::col(export_failed ? palette.error : palette.success));
			ImGui::TextWrapped("%s", export_message.c_str());
			ImGui::PopStyleColor();
		}
	}
	ImGui::End();
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
}
