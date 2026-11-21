// Export : produit un jeu autonome (exécutable + archive de données .pck).
#include "editor_node.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/pack.h"
#include "core/os/os.h"
#include "core/string/ustring.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>

namespace fs = std::filesystem;

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
	ImGui::SetNextWindowSizeConstraints(ImVec2(640, 0), ImVec2(640, FLT_MAX));
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
		if (ImGui::Button("Exporter", ImVec2(120, 0))) {
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
		ImGui::SameLine();
		if (ImGui::Button("Fermer", ImVec2(120, 0))) {
			export_open = false;
		}
		if (!export_message.empty()) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, export_failed ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.55f, 0.9f, 0.55f, 1.0f));
			ImGui::TextWrapped("%s", export_message.c_str());
			ImGui::PopStyleColor();
		}
	}
	ImGui::End();
}
