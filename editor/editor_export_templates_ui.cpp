// Fenêtre « Modèles d'exportation » : état du modèle de chaque système, installation d'un
// paquet « .tpz », suppression des modèles installés.
#include "editor_node.h"

#include "core/os/os.h"
#include "editor_export.h"
#include "editor_ui.h"

#include "imgui.h"

void EditorNode::_draw_export_templates_dialog() {
	if (!templates_open) {
		return;
	}
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(EditorUI::dialog_size(680, 420), ImGuiCond_Appearing);
	if (ImGui::Begin(ICON_PACKAGE "  Modèles d'exportation###ExportTemplates", &templates_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
		EditorUI::hint("Un modèle est l'exécutable « mex-jeu » d'un système : l'export y joint votre jeu. Le paquet « .tpz » d'une version de MEX les réunit tous ; il se télécharge avec l'éditeur, sur la page de la version.");
		ImGui::Spacing();
		ImGui::Text("Version de l'éditeur : %s", MEX_VERSION);
		const String installed = EditorExport::get_installed_templates_version();
		ImGui::SameLine(0.0f, ES(24));
		ImGui::TextDisabled("Paquet installé : %s", installed.empty() ? "aucun" : installed.c_str());
		if (!installed.empty() && installed != MEX_VERSION) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(EditorUI::col(palette.warning), ICON_WARNING "  Le paquet installé vient d'une autre version : installez celui de MEX %s.", MEX_VERSION);
			ImGui::PopTextWrapPos();
		}
		ImGui::Spacing();
		if (ImGui::BeginTable("##modeles", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Système", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Modèle", ImGuiTableColumnFlags_WidthStretch, 2.2f);
			ImGui::TableSetupColumn("Fichier", ImGuiTableColumnFlags_WidthStretch, 2.2f);
			ImGui::TableHeadersRow();
			for (const EditorExport::TemplateInfo &info : EditorExport::get_template_report()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(info.platform.c_str());
				ImGui::TableNextColumn();
				if (info.path.empty()) {
					ImGui::TextColored(EditorUI::col(palette.warning), ICON_WARNING "  absent");
				} else {
					ImGui::TextColored(EditorUI::col(palette.success), ICON_CHECK);
					ImGui::SameLine();
					ImGui::TextUnformatted(info.origin.c_str());
				}
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", info.path.empty() ? "-" : info.path.c_str());
				if (!info.path.empty()) {
					ImGui::SetItemTooltip("%s", info.path.c_str());
				}
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		if (_accent_button(ICON_DOWNLOAD "  Installer depuis un fichier...")) {
			_open_file_dialog("Paquet de modèles d'exportation", { "tpz", "zip" }, false, [this](const String &p_path) {
				templates_ok = EditorExport::install_template_package(p_path, templates_message);
			});
		}
		ImGui::SetItemTooltip("Choisir le fichier mex-<version>-modeles-export.tpz");
		ImGui::SameLine();
		ImGui::BeginDisabled(installed.empty());
		if (ImGui::Button(ICON_TRASH "  Supprimer les modèles installés")) {
			templates_ok = EditorExport::remove_installed_templates(templates_message);
		}
		ImGui::EndDisabled();
		if (!templates_message.empty()) {
			ImGui::Spacing();
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(EditorUI::col(templates_ok ? palette.success : palette.warning), "%s", templates_message.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::Spacing();
		EditorUI::hint((String("Dossier d'installation : ") + EditorExport::get_installed_templates_dir()).c_str());
		EditorUI::hint("En ligne de commande : mex --installer-modeles <paquet.tpz>");
	}
	ImGui::End();
}
