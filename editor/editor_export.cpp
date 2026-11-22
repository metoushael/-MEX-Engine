// Export : produit un jeu autonome (exécutable + archive de données .pck).
#include "editor_node.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/pack.h"
#include "core/os/os.h"
#include "core/string/ustring.h"

#include "editor_export.h"
#include "editor_ui.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>

namespace fs = std::filesystem;

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
		ImGui::SameLine();
		if (ImGui::Button("Fermer", ImVec2(ES(120), 0))) {
			export_open = false;
		}
		if (!export_message.empty()) {
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
}
