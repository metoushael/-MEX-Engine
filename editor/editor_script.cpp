// Éditeur de script intégré et fenêtre « Paramètres du projet ».
#include "editor_node.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/input/keyboard.h"
#include "core/io/file_access.h"
#include "core/io/resource.h"
#include "core/object/script_language.h"
#include "core/string/ustring.h"
#include "modules/mexscript/mexscript.h"
#include "modules/mexscript/mexscript_parser.h"
#include "scene/main/scene_tree.h"

#include "editor_ui.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cstring>
#include <map>

// ---------------------------------------------------------------------------
// Éditeur de script
// ---------------------------------------------------------------------------
void EditorNode::_open_script(const String &p_path) {
	if (p_path.empty()) {
		return;
	}
	if (script_path != p_path) {
		if (script_dirty) {
			_save_script();
		}
		Error err = OK;
		const String text = FileAccess::get_file_as_string(p_path, &err);
		if (err != OK) {
			print_error("Impossible d'ouvrir le fichier : " + p_path);
			return;
		}
		script_path = p_path;
		script_text = text;
		script_dirty = false;
		script_error.clear();
		script_error_line = 0;
		script_last_length = (int)script_text.size();
		script_mtime = FileAccess::get_modified_time(p_path);
	}
	focus_script = true;
}

void EditorNode::_reload_script_resource(const String &p_path, const String &p_source) {
	script_error.clear();
	script_error_line = 0;
	if (path_get_extension(p_path) != "mx") {
		return;
	}
	// Si le script est déjà chargé (nœuds de la scène, jeu en cours), on le recompile sur place :
	// les variables exportées de l'inspecteur et le jeu lancé suivent la modification.
	Ref<MexScript> script = Ref<MexScript>(Object::cast_to<MexScript>(ResourceCache::get(p_path).ptr()));
	if (script.is_valid()) {
		script->set_source_code(p_source);
		if (script->reload() != OK) {
			script_error = script->get_error_message();
			script_error_line = script->get_error_line();
		}
		return;
	}
	MXParseResult result;
	MexScriptParser::parse(p_source, result);
	if (!result.has_error && !ClassDB::class_exists(result.extends)) {
		result.has_error = true;
		result.error = "Classe de base inconnue : '" + result.extends + "'.";
		result.error_line = 1;
	}
	if (result.has_error) {
		script_error = result.error;
		script_error_line = result.error_line;
		print_error(vformat("Script : %s:%d - %s", p_path.c_str(), result.error_line, result.error.c_str()));
	}
}

void EditorNode::_save_script() {
	if (script_path.empty()) {
		return;
	}
	if (FileAccess::write_string(script_path, script_text) != OK) {
		print_error("Impossible d'enregistrer : " + script_path);
		return;
	}
	script_dirty = false;
	script_mtime = FileAccess::get_modified_time(script_path);
	_reload_script_resource(script_path, script_text);
	if (script_error.empty()) {
		print_line("Script enregistré : " + script_path);
	}
}

void EditorNode::_check_external_changes() {
	if (script_path.empty()) {
		return;
	}
	if (!FileAccess::exists(script_path)) {
		if (!script_dirty) {
			script_path.clear();
			script_text.clear();
		}
		return;
	}
	const uint64_t mtime = FileAccess::get_modified_time(script_path);
	if (mtime == script_mtime) {
		return;
	}
	script_mtime = mtime;
	if (script_dirty) {
		print_warning("Le fichier « " + script_path + " » a été modifié en dehors de l'éditeur ; la version de l'éditeur est conservée.");
		return;
	}
	script_text = FileAccess::get_file_as_string(script_path);
	script_last_length = (int)script_text.size();
	_reload_script_resource(script_path, script_text);
	print_line("Script rechargé depuis le disque : " + script_path);
}

namespace {

struct ScriptEditState {
	int *cursor_line;
	int *cursor_column;
	int *last_length;
	int goto_line; // > 0 : placer le curseur sur cette ligne
};

int _script_edit_callback(ImGuiInputTextCallbackData *p_data) {
	ScriptEditState *state = (ScriptEditState *)p_data->UserData;

	if (state->goto_line > 0) {
		int line = 1, pos = 0;
		while (pos < p_data->BufTextLen && line < state->goto_line) {
			if (p_data->Buf[pos] == '\n') {
				line++;
			}
			pos++;
		}
		p_data->CursorPos = pos;
		p_data->SelectionStart = p_data->SelectionEnd = pos;
		state->goto_line = 0;
	}

	// Indentation automatique : après un retour à la ligne, on reprend l'indentation de la ligne
	// précédente (+ une tabulation si elle se termine par « : »).
	const int cursor = p_data->CursorPos;
	if (p_data->BufTextLen == *state->last_length + 1 && cursor > 0 && p_data->Buf[cursor - 1] == '\n' && !p_data->HasSelection()) {
		int line_start = cursor - 1;
		while (line_start > 0 && p_data->Buf[line_start - 1] != '\n') {
			line_start--;
		}
		std::string indent;
		int i = line_start;
		while (i < cursor - 1 && (p_data->Buf[i] == '\t' || p_data->Buf[i] == ' ')) {
			indent += p_data->Buf[i];
			i++;
		}
		int last = cursor - 2;
		while (last >= line_start && (p_data->Buf[last] == ' ' || p_data->Buf[last] == '\t')) {
			last--;
		}
		if (last >= line_start && p_data->Buf[last] == ':') {
			indent += '\t';
		}
		if (!indent.empty()) {
			p_data->InsertChars(cursor, indent.c_str());
		}
	}
	*state->last_length = p_data->BufTextLen;

	int line = 1, column = 1;
	for (int i = 0; i < p_data->CursorPos && i < p_data->BufTextLen; i++) {
		if (p_data->Buf[i] == '\n') {
			line++;
			column = 1;
		} else if ((p_data->Buf[i] & 0xC0) != 0x80) { // ne compte pas les octets de suite UTF-8
			column++;
		}
	}
	*state->cursor_line = line;
	*state->cursor_column = column;
	return 0;
}

} // namespace

void EditorNode::_draw_script_dock() {
	if (focus_script) {
		focus_script = false;
		ImGui::SetNextWindowFocus();
	}
	const bool visible = ImGui::Begin(ICON_DOCK_SCRIPT "  Script###Script", nullptr, script_dirty ? ImGuiWindowFlags_UnsavedDocument : 0);
	if (!visible) {
		ImGui::End();
		return;
	}
	workspace_active = WORKSPACE_SCRIPT;
	if (script_path.empty()) {
		ImGui::Spacing();
		ImGui::TextDisabled("Aucun script ouvert.");
		ImGui::TextWrapped("Double-cliquez sur un fichier .mx dans le dock « Fichiers », ou faites un clic droit sur un nœud puis « Attacher un script ».");
		ImGui::End();
		return;
	}

	if (ImGui::Button("Enregistrer")) {
		_save_script();
	}
	ImGui::SetItemTooltip("Enregistre et vérifie le script (Ctrl+S)");
	ImGui::SameLine();
	if (ImGui::Button("Fermer")) {
		if (script_dirty) {
			_confirm("Script non enregistré", "Enregistrer les modifications de « " + script_path + " » ?", {
																												   { "Enregistrer", [this]() {
																														_save_script();
																														script_path.clear();
																														script_text.clear();
																													} },
																												   { "Ne pas enregistrer", [this]() {
																														script_dirty = false;
																														script_path.clear();
																														script_text.clear();
																													} },
																												   { "Annuler", nullptr },
																										   });
		} else {
			script_path.clear();
			script_text.clear();
		}
	}
	ImGui::SameLine();
	ImGui::TextUnformatted((script_path + (script_dirty ? " (*)" : "")).c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("  Ligne %d, colonne %d", script_cursor_line, script_cursor_column);

	const float footer = script_error.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing() + 4;
	ScriptEditState state{ &script_cursor_line, &script_cursor_column, &script_last_length, 0 };
	if (script_goto_error) {
		script_goto_error = false;
		state.goto_line = script_error_line;
		ImGui::SetKeyboardFocusHere();
	}
	if (font_mono) {
		ImGui::PushFont(font_mono, 0.0f);
	}
	const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways;
	if (ImGui::InputTextMultiline("##code", &script_text, ImVec2(-FLT_MIN, -footer - 1), flags, _script_edit_callback, &state)) {
		script_dirty = true;
	}
	if (font_mono) {
		ImGui::PopFont();
	}
	if (ImGui::IsItemActive() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
		// Géré par le raccourci global ; rien à faire ici.
	}

	if (!script_error.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, EditorUI::col(palette.error));
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Ligne %d : %s", script_error_line, script_error.c_str());
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (ImGui::SmallButton("Aller à l'erreur")) {
			script_goto_error = true;
		}
	}
	ImGui::End();
}

// ---------------------------------------------------------------------------
// Paramètres du projet
// ---------------------------------------------------------------------------
void EditorNode::_save_input_map() {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	// Retire les actions supprimées.
	std::vector<String> stale;
	for (const auto &entry : ps->get_all_settings()) {
		if (str_begins_with(entry.first, "input/") && !InputMap::get_singleton()->has_action(entry.first.substr(6))) {
			stale.push_back(entry.first);
		}
		if (str_begins_with(entry.first, "input_deadzones/")) {
			stale.push_back(entry.first); // réécrites plus bas pour les actions qui en ont une
		}
	}
	for (const String &key : stale) {
		ps->erase_setting(key);
	}
	for (const auto &entry : InputMap::get_singleton()->get_action_map()) {
		Array codes;
		for (int code : entry.second) {
			codes.push_back(Variant(input_code_get_name(code)));
		}
		ps->set_setting("input/" + entry.first, Variant(codes));
		if (InputMap::get_singleton()->action_has_deadzone(entry.first)) {
			ps->set_setting("input_deadzones/" + entry.first, Variant((double)InputMap::get_singleton()->action_get_deadzone(entry.first)));
		}
	}
	if (ps->save() != OK) {
		print_error("Impossible d'enregistrer les paramètres du projet.");
	}
}

// Nom affiché d'une entrée de manette ("JoyLeftX+" reste le nom écrit dans project.mex).
static String _joy_code_label(int p_code) {
	static const std::map<String, const char *> labels = {
		{ "JoyA", "Manette : A (croix sur PlayStation)" },
		{ "JoyB", "Manette : B (rond)" },
		{ "JoyX", "Manette : X (carré)" },
		{ "JoyY", "Manette : Y (triangle)" },
		{ "JoyBack", "Manette : Back / Select" },
		{ "JoyGuide", "Manette : bouton central" },
		{ "JoyStart", "Manette : Start" },
		{ "JoyLeftStick", "Manette : clic du stick gauche" },
		{ "JoyRightStick", "Manette : clic du stick droit" },
		{ "JoyLeftShoulder", "Manette : gâchette haute gauche (LB)" },
		{ "JoyRightShoulder", "Manette : gâchette haute droite (RB)" },
		{ "JoyDpadUp", "Manette : croix haut" },
		{ "JoyDpadDown", "Manette : croix bas" },
		{ "JoyDpadLeft", "Manette : croix gauche" },
		{ "JoyDpadRight", "Manette : croix droite" },
		{ "JoyLeftX-", "Manette : stick gauche vers la gauche" },
		{ "JoyLeftX+", "Manette : stick gauche vers la droite" },
		{ "JoyLeftY-", "Manette : stick gauche vers le haut" },
		{ "JoyLeftY+", "Manette : stick gauche vers le bas" },
		{ "JoyRightX-", "Manette : stick droit vers la gauche" },
		{ "JoyRightX+", "Manette : stick droit vers la droite" },
		{ "JoyRightY-", "Manette : stick droit vers le haut" },
		{ "JoyRightY+", "Manette : stick droit vers le bas" },
		{ "JoyTriggerLeft", "Manette : gâchette gauche (LT)" },
		{ "JoyTriggerRight", "Manette : gâchette droite (RT)" },
	};
	const String name = input_code_get_name(p_code);
	const auto it = labels.find(name);
	return it == labels.end() ? name : String(it->second);
}

void EditorNode::_draw_project_settings() {
	if (!settings_open) {
		if (!capture_action.empty()) {
			capture_action.clear();
		}
		return;
	}
	ProjectSettings *ps = ProjectSettings::get_singleton();
	bool changed = false;

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(EditorUI::dialog_size(640, 560), ImGuiCond_Appearing);
	if (!ImGui::Begin("Paramètres du projet###ProjectSettings", &settings_open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	auto edit_string = [&](const char *p_label, const char *p_key) {
		std::string value = ps->get_setting(p_key).as_string();
		ImGui::SetNextItemWidth(ES(340));
		if (ImGui::InputText(p_label, &value)) {
			ps->set_setting(p_key, Variant(String(value)));
			changed = true;
		}
	};
	auto edit_int = [&](const char *p_label, const char *p_key, int p_min, int p_max) {
		int value = (int)ps->get_setting(p_key).as_int();
		ImGui::SetNextItemWidth(ES(160));
		if (ImGui::InputInt(p_label, &value, 0, 0)) {
			ps->set_setting(p_key, Variant((int64_t)std::clamp(value, p_min, p_max)));
			changed = true;
		}
	};
	auto edit_bool = [&](const char *p_label, const char *p_key) {
		bool value = ps->get_setting(p_key).as_bool();
		if (ImGui::Checkbox(p_label, &value)) {
			ps->set_setting(p_key, Variant(value));
			changed = true;
		}
	};

	if (ImGui::BeginTabBar("##settings_tabs")) {
		if (ImGui::BeginTabItem("Général")) {
			ImGui::SeparatorText("Application");
			edit_string("Nom du jeu", "application/name");
			edit_string("Icône de la fenêtre", "application/icon");
			ImGui::SetItemTooltip("Image du projet, par exemple res://icone.png. Vide : icône de MEX.");
			{
				const String main_scene = ps->get_setting("application/main_scene").as_string();
				ImGui::SetNextItemWidth(ES(340));
				if (ImGui::BeginCombo("Scène principale", main_scene.empty() ? "<aucune>" : main_scene.c_str())) {
					for (const String &file : fs_files) {
						if (path_get_extension(file) != "scn") {
							continue;
						}
						if (ImGui::Selectable(file.c_str(), file == main_scene)) {
							ps->set_setting("application/main_scene", Variant(file));
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Scène lancée par F5 et au démarrage du jeu");
			}

			ImGui::SeparatorText("Affichage");
			edit_int("Largeur", "display/width", 64, 7680);
			edit_int("Hauteur", "display/height", 64, 4320);
			edit_bool("Fenêtre redimensionnable", "display/resizable");
			edit_int("Largeur minimale", "display/min_width", 0, 7680);
			ImGui::SetItemTooltip("Le joueur ne peut pas réduire la fenêtre du jeu en dessous de cette largeur (0 : pas de limite)");
			edit_int("Hauteur minimale", "display/min_height", 0, 4320);
			ImGui::SetItemTooltip("Le joueur ne peut pas réduire la fenêtre du jeu en dessous de cette hauteur (0 : pas de limite)");
			edit_bool("Barre de titre sombre", "display/dark_title_bar");
			ImGui::SetItemTooltip("Barre de titre de la fenêtre du jeu en version sombre (Windows 10 et 11, GNOME et environnements proches)");
			edit_bool("Plein écran", "display/fullscreen");
			edit_bool("Synchronisation verticale", "display/vsync");

			ImGui::SeparatorText("Rendu");
			{
				Color c = ps->get_setting("rendering/clear_color").as_color();
				float f[4] = { c.r, c.g, c.b, c.a };
				ImGui::SetNextItemWidth(ES(340));
				if (ImGui::ColorEdit3("Couleur de fond", f)) {
					ps->set_setting("rendering/clear_color", Variant(Color(f[0], f[1], f[2], 1.0f)));
					changed = true;
				}
				const String filter = ps->get_setting("rendering/texture_filter").as_string();
				ImGui::SetNextItemWidth(ES(340));
				if (ImGui::BeginCombo("Filtre des textures", filter == "nearest" ? "Pixels nets (nearest)" : "Lissé (linear)")) {
					if (ImGui::Selectable("Lissé (linear)", filter != "nearest")) {
						ps->set_setting("rendering/texture_filter", Variant(String("linear")));
						changed = true;
					}
					if (ImGui::Selectable("Pixels nets (nearest)", filter == "nearest")) {
						ps->set_setting("rendering/texture_filter", Variant(String("nearest")));
						changed = true;
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("« Pixels nets » convient au pixel art.\nS'applique aux textures chargées ensuite.");
			}

			ImGui::SeparatorText("Physique");
			{
				float gravity = (float)ps->get_setting("physics/gravity").as_float();
				ImGui::SetNextItemWidth(ES(160));
				if (ImGui::InputFloat("Gravité (px/s²)", &gravity, 0, 0, "%.1f")) {
					ps->set_setting("physics/gravity", Variant((double)gravity));
					changed = true;
				}
			}
			edit_int("Pas de physique par seconde", "physics/ticks_per_second", 10, 240);
			edit_bool("Afficher les formes de collision en jeu", "debug/visible_collision_shapes");
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Contrôles")) {
			ImGui::TextWrapped("Les actions s'utilisent dans les scripts : Input.is_action_pressed(\"sauter\"), Input.get_axis(\"gauche\", \"droite\")...");
			ImGui::Spacing();
			ImGui::SetNextItemWidth(ES(260));
			const bool entered = ImGui::InputTextWithHint("##new_action", "Nom de la nouvelle action", &new_action_name, ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if (ImGui::Button("Ajouter l'action") || entered) {
				const String name = str_strip(new_action_name);
				if (!str_is_valid_identifier(name)) {
					print_warning("Nom d'action invalide (lettres, chiffres et _ uniquement) : " + name);
				} else if (!InputMap::get_singleton()->has_action(name)) {
					InputMap::get_singleton()->add_action(name);
					_save_input_map();
					new_action_name.clear();
				}
			}
			ImGui::Separator();

			ImGui::BeginChild("##actions", ImVec2(0, 0));
			// Copie : la table peut être modifiée pendant le parcours.
			const std::map<String, std::vector<int>> actions = InputMap::get_singleton()->get_action_map();
			for (const auto &entry : actions) {
				const String &action = entry.first;
				ImGui::PushID(action.c_str());
				const bool open = ImGui::TreeNodeEx("##action", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap, "%s", action.c_str());
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 330);
				if (capture_action == action) {
					ImGui::PushStyleColor(ImGuiCol_Button, EditorUI::col(palette.accent, 0.55f));
					if (ImGui::SmallButton("Appuyez sur une touche... (Échap)")) {
						capture_action.clear();
					}
					ImGui::PopStyleColor();
				} else {
					if (ImGui::SmallButton("+ Touche")) {
						capture_action = action;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("+ Souris")) {
						ImGui::OpenPopup("##mouse");
					}
					if (ImGui::BeginPopup("##mouse")) {
						const std::pair<const char *, int> buttons[] = {
							{ "Bouton gauche", MOUSE_BUTTON_LEFT },
							{ "Bouton droit", MOUSE_BUTTON_RIGHT },
							{ "Bouton du milieu", MOUSE_BUTTON_MIDDLE },
						};
						for (const auto &button : buttons) {
							if (ImGui::MenuItem(button.first)) {
								InputMap::get_singleton()->action_add_code(action, INPUT_CODE_MOUSE_FLAG | button.second);
								_save_input_map();
							}
						}
						ImGui::EndPopup();
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("+ Manette")) {
						ImGui::OpenPopup("##joypad");
					}
					if (ImGui::BeginPopup("##joypad")) {
						std::vector<std::pair<String, int>> joy_codes;
						input_code_get_joy_names(&joy_codes);
						for (const auto &joy : joy_codes) {
							if (joy.first == "JoyLeftX-") {
								ImGui::Separator(); // boutons au-dessus, sticks et gâchettes en dessous
							}
							// Sans le préfixe « Manette : », déjà dit par le bouton.
							if (ImGui::MenuItem(_joy_code_label(joy.second).substr(strlen("Manette : ")).c_str())) {
								InputMap::get_singleton()->action_add_code(action, joy.second);
								_save_input_map();
							}
						}
						ImGui::EndPopup();
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Supprimer")) {
						InputMap::get_singleton()->erase_action(action);
						_save_input_map();
					}
				}
				if (open) {
					if (entry.second.empty()) {
						ImGui::TextDisabled("Aucune touche assignée.");
					}
					int remove_code = 0;
					for (int code : entry.second) {
						ImGui::PushID(code);
						if (ImGui::SmallButton("x")) {
							remove_code = code;
						}
						ImGui::SameLine();
						ImGui::TextUnformatted(_joy_code_label(code).c_str());
						ImGui::PopID();
					}
					// Zone morte : seulement utile si un stick ou une gâchette est assigné.
					bool has_axis = false;
					for (int code : entry.second) {
						has_axis = has_axis || input_code_is_joy_axis(code);
					}
					if (has_axis && InputMap::get_singleton()->has_action(action)) {
						float deadzone = InputMap::get_singleton()->action_get_deadzone(action);
						ImGui::SetNextItemWidth(160);
						if (ImGui::SliderFloat("Zone morte", &deadzone, 0.0f, 1.0f, "%.2f")) {
							InputMap::get_singleton()->action_set_deadzone(action, deadzone);
						}
						if (ImGui::IsItemDeactivatedAfterEdit()) {
							_save_input_map();
						}
						ImGui::SameLine();
						ImGui::TextDisabled("(en dessous, le stick est ignoré)");
					}
					if (remove_code != 0 && InputMap::get_singleton()->has_action(action)) {
						InputMap::get_singleton()->action_erase_codes(action);
						for (int code : entry.second) {
							if (code != remove_code) {
								InputMap::get_singleton()->action_add_code(action, code);
							}
						}
						_save_input_map();
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();

	if (changed) {
		if (ps->save() != OK) {
			print_error("Impossible d'enregistrer les paramètres du projet.");
		}
		_apply_project_settings();
		if (edit_tree) {
			edit_tree->set_viewport_size(Size2((real_t)GLOBAL_GET("display/width").as_int(), (real_t)GLOBAL_GET("display/height").as_int()));
		}
		_update_window_title();
	}
}
