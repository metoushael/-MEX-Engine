// Docks « Scène », « Fichiers », « Sortie » et boîtes de dialogue communes.
#include "editor_node.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/string/ustring.h"
#include "scene/main/canvas_item.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/packed_scene.h"
#include "core/object/script_language.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace {

// Couleur d'un type de nœud dans l'arbre (comme les familles d'icônes de Godot).
ImVec4 _class_color(const String &p_class) {
	if (ClassDB::is_parent_class(p_class, "Control")) {
		return ImVec4(0.56f, 0.86f, 0.56f, 1.0f);
	}
	if (ClassDB::is_parent_class(p_class, "Node2D")) {
		return ImVec4(0.55f, 0.72f, 0.98f, 1.0f);
	}
	if (ClassDB::is_parent_class(p_class, "Node3D")) {
		return ImVec4(0.98f, 0.60f, 0.56f, 1.0f);
	}
	if (ClassDB::is_parent_class(p_class, "CanvasLayer")) {
		return ImVec4(0.85f, 0.75f, 0.50f, 1.0f);
	}
	return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
}

bool _extension_matches(const String &p_path, const std::vector<String> &p_extensions) {
	if (p_extensions.empty()) {
		return true;
	}
	const String ext = path_get_extension(p_path);
	return std::find(p_extensions.begin(), p_extensions.end(), ext) != p_extensions.end();
}

} // namespace

// ---------------------------------------------------------------------------
// Dock « Scène »
// ---------------------------------------------------------------------------
void EditorNode::_draw_scene_dock() {
	if (!ImGui::Begin("Scène###SceneDock")) {
		ImGui::End();
		return;
	}

	if (!scene_root) {
		ImGui::TextWrapped("La scène est vide. Choisissez un nœud racine :");
		ImGui::Spacing();
		const char *types[][2] = { { "Scène 2D", "Node2D" }, { "Interface", "Control" }, { "Nœud simple", "Node" } };
		for (auto &type : types) {
			if (ImGui::Button(type[0], ImVec2(-FLT_MIN, 0))) {
				const String cls = type[1];
				_defer([this, cls]() { _create_node(cls, nullptr); });
			}
		}
		if (ImGui::Button("Autre nœud...", ImVec2(-FLT_MIN, 0))) {
			create_dialog.open_request = true;
			create_dialog.as_root = true;
			create_dialog.parent_id = 0;
		}
		ImGui::End();
		return;
	}

	Node *selected = _get_selected();
	if (ImGui::Button("+ Nœud")) {
		create_dialog.open_request = true;
		create_dialog.as_root = false;
		create_dialog.parent_id = (selected && _is_editable(selected) ? selected : scene_root)->get_instance_id();
	}
	ImGui::SetItemTooltip("Ajouter un nœud enfant au nœud sélectionné");
	ImGui::SameLine();
	if (ImGui::Button("+ Scène")) {
		const ObjectID parent_id = (selected && _is_editable(selected) ? selected : scene_root)->get_instance_id();
		_open_file_dialog("Instancier une scène", { "scn" }, false, [this, parent_id](const String &p_path) {
			_instance_scene(p_path, Object::cast_to<Node>(ObjectDB::get_instance(parent_id)));
		});
	}
	ImGui::SetItemTooltip("Instancier une scène enregistrée comme enfant du nœud sélectionné");
	ImGui::Separator();

	ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	_draw_scene_node(scene_root);

	// Raccourcis quand le dock a le focus.
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && rename_id == 0) {
		Node *current = _get_selected();
		if (current && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
			_defer([this, current]() { _delete_node(current); });
		}
		if (current && ImGui::IsKeyPressed(ImGuiKey_F2)) {
			rename_id = current->get_instance_id();
			rename_buffer = current->get_name();
			rename_focus = true;
		}
		if (current && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
			_defer([this, current]() { _duplicate_node(current); });
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

void EditorNode::_draw_scene_node(Node *p_node) {
	const ObjectID id = p_node->get_instance_id();
	bool has_visible_children = false;
	for (Node *child : p_node->get_children_vector()) {
		if (_is_editable(child)) {
			has_visible_children = true;
			break;
		}
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (!has_visible_children) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (id == selected_id) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	const bool is_instance = p_node != scene_root && !p_node->get_scene_file_path().empty();
	const bool has_script = !p_node->get_script().is_null();
	CanvasItem *canvas_item = Object::cast_to<CanvasItem>(p_node);
	const bool hidden = canvas_item && !canvas_item->is_visible();

	ImGui::PushID((int)id);
	bool open = false;
	if (rename_id == id) {
		open = ImGui::TreeNodeEx("##node", flags | ImGuiTreeNodeFlags_AllowOverlap, "%s", "");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (rename_focus) {
			ImGui::SetKeyboardFocusHere();
			rename_focus = false;
		}
		const bool entered = ImGui::InputText("##rename", &rename_buffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (entered || ImGui::IsItemDeactivated()) {
			if (entered || !ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				const String new_name = str_strip(rename_buffer);
				if (!new_name.empty() && new_name != p_node->get_name()) {
					p_node->set_name(new_name);
					_mark_modified();
				}
			}
			rename_id = 0;
		}
	} else {
		ImVec4 color = _class_color(p_node->get_class());
		if (hidden) {
			color.w = 0.45f;
		}
		String label = p_node->get_name();
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
		ImGui::PopStyleColor();

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
			_select(p_node);
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !has_visible_children) {
			rename_id = id;
			rename_buffer = p_node->get_name();
			rename_focus = true;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
			String tip = p_node->get_class();
			if (is_instance) {
				tip += "\nInstance de " + p_node->get_scene_file_path();
			}
			if (has_script) {
				Resource *script = Object::cast_to<Resource>(p_node->get_script().as_object());
				tip += "\nScript : " + (script ? script->get_path() : String("?"));
			}
			ImGui::SetTooltip("%s", tip.c_str());
		}

		// Glisser-déposer : changer de parent.
		if (p_node != scene_root && ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("MEX_NODE", &id, sizeof(ObjectID));
			ImGui::Text("Déplacer %s", p_node->get_name().c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MEX_NODE")) {
				const ObjectID dragged_id = *(const ObjectID *)payload->Data;
				_defer([this, dragged_id, id]() {
					Node *dragged = Object::cast_to<Node>(ObjectDB::get_instance(dragged_id));
					Node *target = Object::cast_to<Node>(ObjectDB::get_instance(id));
					if (!dragged || !target || dragged == target || dragged->is_ancestor_of(target) || dragged->get_parent() == target) {
						return;
					}
					dragged->reparent(target);
					dragged->set_owner(scene_root);
					_mark_modified();
				});
			}
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MEX_FILE")) {
				const String path((const char *)payload->Data);
				_defer([this, path, id]() {
					Node *target = Object::cast_to<Node>(ObjectDB::get_instance(id));
					if (!target) {
						return;
					}
					const String ext = path_get_extension(path);
					if (ext == "scn") {
						_instance_scene(path, target);
					} else if (ext == "mx") {
						_attach_script(target, path);
					}
				});
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextItem("##ctx")) {
			_select(p_node);
			_node_context_menu(p_node);
			ImGui::EndPopup();
		}

		// Pastilles à droite : script, instance.
		String badges;
		if (is_instance) {
			badges += " [scène]";
		}
		if (has_script) {
			badges += " [script]";
		}
		if (!badges.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", badges.c_str());
		}
	}

	if (open) {
		// Copie : la liste peut changer via les actions différées, pas pendant le parcours.
		for (Node *child : p_node->get_children_vector()) {
			if (_is_editable(child)) {
				_draw_scene_node(child);
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void EditorNode::_node_context_menu(Node *p_node) {
	const ObjectID id = p_node->get_instance_id();
	const bool is_root = p_node == scene_root;
	if (ImGui::MenuItem("Ajouter un nœud enfant...")) {
		create_dialog.open_request = true;
		create_dialog.as_root = false;
		create_dialog.parent_id = id;
	}
	if (ImGui::MenuItem("Instancier une scène enfant...")) {
		_open_file_dialog("Instancier une scène", { "scn" }, false, [this, id](const String &p_path) {
			_instance_scene(p_path, Object::cast_to<Node>(ObjectDB::get_instance(id)));
		});
	}
	ImGui::Separator();
	if (p_node->get_script().is_null()) {
		if (ImGui::MenuItem("Attacher un script...")) {
			const String suggestion = "res://" + str_to_lower(p_node->get_name()) + ".mx";
			_open_file_dialog("Attacher un script (nouveau ou existant)", { "mx" }, true, [this, id](const String &p_path) {
				_attach_script(Object::cast_to<Node>(ObjectDB::get_instance(id)), p_path);
			}, suggestion);
		}
	} else {
		if (ImGui::MenuItem("Modifier le script")) {
			if (Resource *script = Object::cast_to<Resource>(p_node->get_script().as_object())) {
				const String path = script->get_path();
				_defer([this, path]() { _open_script(path); });
			}
		}
		if (ImGui::MenuItem("Détacher le script")) {
			_defer([this, id]() {
				if (Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id))) {
					node->set("script", Variant());
					_mark_modified();
				}
			});
		}
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Renommer", "F2")) {
		rename_id = id;
		rename_buffer = p_node->get_name();
		rename_focus = true;
	}
	if (ImGui::MenuItem("Dupliquer", "Ctrl+D", false, !is_root)) {
		_defer([this, id]() { _duplicate_node(Object::cast_to<Node>(ObjectDB::get_instance(id))); });
	}
	if (ImGui::MenuItem("Monter", nullptr, false, !is_root && p_node->get_index() > 0)) {
		_defer([this, id]() {
			Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
			if (node && node->get_parent()) {
				node->get_parent()->move_child(node, node->get_index() - 1);
				_mark_modified();
			}
		});
	}
	if (ImGui::MenuItem("Descendre", nullptr, false, !is_root && p_node->get_parent() && p_node->get_index() < p_node->get_parent()->get_child_count() - 1)) {
		_defer([this, id]() {
			Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
			if (node && node->get_parent()) {
				node->get_parent()->move_child(node, node->get_index() + 1);
				_mark_modified();
			}
		});
	}
	if (ImGui::MenuItem("Enregistrer la branche comme scène...", nullptr, false, !is_root)) {
		_open_file_dialog("Enregistrer la branche comme scène", { "scn" }, true, [this, id](const String &p_path) {
			Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
			if (!node || !scene_root) {
				return;
			}
			String path = str_begins_with(p_path, "res://") ? p_path : "res://" + p_path;
			if (path_get_extension(path) != "scn") {
				path += ".scn";
			}
			// La branche est copiée, enregistrée, puis remplacée par une instance de la nouvelle scène.
			Node *copy = nullptr;
			{
				ScriptingDisabledScope no_scripts;
				copy = node->duplicate();
			}
			if (!copy) {
				return;
			}
			std::function<void(Node *)> own = [&](Node *p_n) {
				for (Node *c : p_n->get_children_vector()) {
					if (c->get_owner() == scene_root || c->get_owner() == copy) {
						c->set_owner(copy);
						own(c);
					}
				}
			};
			copy->set_owner(nullptr);
			own(copy);
			Ref<PackedScene> packed;
			packed.instantiate();
			const Error err = packed->pack(copy) == OK ? ResourceSaver::save(path, packed) : FAILED;
			memdelete(copy);
			if (err != OK) {
				print_error("Impossible d'enregistrer la branche : " + path);
				return;
			}
			Ref<PackedScene> cached = ResourceCache::get(path);
			if (cached.is_valid()) {
				cached->load_from_text(packed->save_to_text(), path);
			}
			Node *parent = node->get_parent();
			const int index = node->get_index();
			const String name = node->get_name();
			memdelete(node);
			if (Node *instance = _instance_scene(path, parent)) {
				instance->set_name(name);
				parent->move_child(instance, index);
			}
			fs_dirty = true;
		}, "res://" + str_to_lower(p_node->get_name()) + ".scn");
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Supprimer", "Suppr")) {
		_defer([this, id]() { _delete_node(Object::cast_to<Node>(ObjectDB::get_instance(id))); });
	}
}

// ---------------------------------------------------------------------------
// Dock « Fichiers »
// ---------------------------------------------------------------------------
void EditorNode::_scan_dir(FsEntry &r_entry) {
	std::vector<String> files, dirs;
	FileAccess::list_dir(r_entry.path, &files, &dirs);
	for (const String &d : dirs) {
		FsEntry child;
		child.name = d;
		child.path = path_join(r_entry.path, d);
		child.is_dir = true;
		_scan_dir(child);
		r_entry.children.push_back(child);
	}
	for (const String &f : files) {
		if (f == ProjectSettings::PROJECT_FILE) {
			continue;
		}
		FsEntry child;
		child.name = f;
		child.path = path_join(r_entry.path, f);
		r_entry.children.push_back(child);
		fs_files.push_back(child.path);
	}
}

void EditorNode::_scan_filesystem() {
	fs_dirty = false;
	fs_root = FsEntry();
	fs_root.name = "res://";
	fs_root.path = "res://";
	fs_root.is_dir = true;
	fs_files.clear();
	_scan_dir(fs_root);
}

void EditorNode::_open_file(const String &p_path) {
	const String ext = path_get_extension(p_path);
	if (ext == "scn") {
		_check_unsaved_then([this, p_path]() { _open_scene(p_path); });
	} else if (ext == "mx" || ext == "txt" || ext == "md" || ext == "json" || ext == "cfg") {
		_open_script(p_path);
	}
}

void EditorNode::_draw_fs_entry(const FsEntry &p_entry) {
	ImGui::PushID(p_entry.path.c_str());
	if (p_entry.is_dir) {
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
		if (p_entry.path == "res://") {
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.80f, 0.55f, 1.0f));
		const bool open = ImGui::TreeNodeEx("##dir", flags, "%s", p_entry.name.c_str());
		ImGui::PopStyleColor();
		if (ImGui::BeginPopupContextItem("##ctx")) {
			if (ImGui::MenuItem("Nouveau dossier...")) {
				fs_new_kind = 1;
				fs_new_dir = p_entry.path;
				fs_new_name = "nouveau_dossier";
			}
			if (ImGui::MenuItem("Nouveau script...")) {
				fs_new_kind = 2;
				fs_new_dir = p_entry.path;
				fs_new_name = "nouveau_script.mx";
			}
			if (ImGui::MenuItem("Actualiser")) {
				fs_dirty = true;
			}
			ImGui::EndPopup();
		}
		if (open) {
			for (const FsEntry &child : p_entry.children) {
				_draw_fs_entry(child);
			}
			ImGui::TreePop();
		}
	} else {
		const String ext = path_get_extension(p_entry.name);
		ImVec4 color(0.82f, 0.84f, 0.86f, 1.0f);
		if (ext == "scn") {
			color = ImVec4(0.55f, 0.72f, 0.98f, 1.0f);
		} else if (ext == "mx") {
			color = ImVec4(0.60f, 0.90f, 0.70f, 1.0f);
		} else if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp") {
			color = ImVec4(0.93f, 0.66f, 0.80f, 1.0f);
		} else if (ext == "wav") {
			color = ImVec4(0.95f, 0.78f, 0.50f, 1.0f);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TreeNodeEx("##file", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", p_entry.name.c_str());
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			const String path = p_entry.path;
			_defer([this, path]() { _open_file(path); });
		}
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("MEX_FILE", p_entry.path.c_str(), p_entry.path.size() + 1);
			ImGui::TextUnformatted(p_entry.path.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginPopupContextItem("##ctx")) {
			const String path = p_entry.path;
			if (ImGui::MenuItem("Ouvrir")) {
				_defer([this, path]() { _open_file(path); });
			}
			if (ext == "scn") {
				if (ImGui::MenuItem("Instancier dans la scène")) {
					_defer([this, path]() {
						Node *selected = _get_selected();
						_instance_scene(path, selected && _is_editable(selected) ? selected : scene_root);
					});
				}
				if (ImGui::MenuItem("Définir comme scène principale")) {
					ProjectSettings::get_singleton()->set_setting("application/main_scene", Variant(path));
					ProjectSettings::get_singleton()->save();
					print_line("Scène principale : " + path);
				}
			}
			if (ImGui::MenuItem("Copier le chemin")) {
				ImGui::SetClipboardText(path.c_str());
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Supprimer...")) {
				_confirm("Supprimer le fichier", "Supprimer définitivement « " + path + " » ?", {
																									   { "Supprimer", [this, path]() {
																											if (FileAccess::remove(path) != OK) {
																												print_error("Impossible de supprimer : " + path);
																											}
																											fs_dirty = true;
																										} },
																									   { "Annuler", nullptr },
																							   });
			}
			ImGui::EndPopup();
		}
	}
	ImGui::PopID();
}

void EditorNode::_draw_filesystem_dock() {
	if (!ImGui::Begin("Fichiers###FilesDock")) {
		ImGui::End();
		return;
	}
	if (ImGui::Button("Actualiser")) {
		fs_dirty = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Script")) {
		fs_new_kind = 2;
		fs_new_dir = "res://";
		fs_new_name = "nouveau_script.mx";
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Dossier")) {
		fs_new_kind = 1;
		fs_new_dir = "res://";
		fs_new_name = "nouveau_dossier";
	}
	ImGui::Separator();
	ImGui::BeginChild("##files", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	_draw_fs_entry(fs_root);
	ImGui::EndChild();
	ImGui::End();

	// Création d'un dossier ou d'un script.
	if (fs_new_kind != 0) {
		ImGui::OpenPopup("###FsNew");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	const char *title = fs_new_kind == 1 ? "Nouveau dossier###FsNew" : "Nouveau script###FsNew";
	if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Dans : %s", fs_new_dir.c_str());
		ImGui::SetNextItemWidth(320);
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		const bool entered = ImGui::InputText("##name", &fs_new_name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (ImGui::Button("Créer", ImVec2(120, 0)) || entered) {
			const String name = str_strip(fs_new_name);
			if (!name.empty()) {
				String path = path_join(fs_new_dir, name);
				if (fs_new_kind == 1) {
					if (FileAccess::make_dir_recursive(path) != OK) {
						print_error("Impossible de créer le dossier : " + path);
					}
				} else {
					if (path_get_extension(path) != "mx") {
						path += ".mx";
					}
					if (FileAccess::exists(path)) {
						print_warning("Le fichier existe déjà : " + path);
					} else if (FileAccess::write_string(path, _make_script_template("Node")) != OK) {
						print_error("Impossible de créer le script : " + path);
					} else {
						_defer([this, path]() { _open_script(path); });
					}
				}
				fs_dirty = true;
			}
			fs_new_kind = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Annuler", ImVec2(120, 0))) {
			fs_new_kind = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// ---------------------------------------------------------------------------
// Dock « Sortie »
// ---------------------------------------------------------------------------
void EditorNode::_draw_console_dock() {
	if (focus_console_frames > 0 && --focus_console_frames == 0) {
		ImGui::SetNextWindowFocus();
		focus_viewport = true; // la vue 2D (dessinée ensuite) reprend le focus clavier
	}
	if (!ImGui::Begin("Sortie###Console")) {
		ImGui::End();
		return;
	}
	if (ImGui::Button("Effacer")) {
		console_lines.clear();
	}
	ImGui::SameLine();
	if (ImGui::Button("Copier")) {
		String all;
		for (const ConsoleLine &line : console_lines) {
			all += line.text + "\n";
		}
		ImGui::SetClipboardText(all.c_str());
	}
	ImGui::SameLine();
	int errors = 0, warnings = 0;
	for (const ConsoleLine &line : console_lines) {
		errors += line.level == PRINT_ERROR ? line.count : 0;
		warnings += line.level == PRINT_WARNING ? line.count : 0;
	}
	ImGui::TextDisabled("%d erreur(s), %d avertissement(s)", errors, warnings);
	ImGui::Separator();

	ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	ImGuiListClipper clipper;
	clipper.Begin((int)console_lines.size());
	while (clipper.Step()) {
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
			const ConsoleLine &line = console_lines[i];
			ImVec4 color(0.82f, 0.84f, 0.86f, 1.0f);
			if (line.level == PRINT_ERROR) {
				color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
			} else if (line.level == PRINT_WARNING) {
				color = ImVec4(1.0f, 0.83f, 0.40f, 1.0f);
			}
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			if (line.count > 1) {
				ImGui::Text("%s  (x%d)", line.text.c_str(), line.count);
			} else {
				ImGui::TextUnformatted(line.text.c_str());
			}
			ImGui::PopStyleColor();
		}
	}
	if (console_scroll) {
		ImGui::SetScrollHereY(1.0f);
		console_scroll = false;
	}
	ImGui::EndChild();
	ImGui::End();
}

// ---------------------------------------------------------------------------
// Boîtes de dialogue
// ---------------------------------------------------------------------------
void EditorNode::_open_file_dialog(const String &p_title, const std::vector<String> &p_extensions, bool p_save, std::function<void(const String &)> p_callback, const String &p_default) {
	file_dialog.open_request = true;
	file_dialog.title = p_title;
	file_dialog.extensions = p_extensions;
	file_dialog.save_mode = p_save;
	file_dialog.callback = std::move(p_callback);
	file_dialog.filter.clear();
	file_dialog.filename = p_default.empty() && p_save ? String("res://") : p_default;
}

void EditorNode::_confirm(const String &p_title, const String &p_message, std::vector<std::pair<String, std::function<void()>>> p_buttons) {
	confirm_dialog.open_request = true;
	confirm_dialog.title = p_title;
	confirm_dialog.message = p_message;
	confirm_dialog.buttons = std::move(p_buttons);
}

void EditorNode::_draw_dialogs() {
	const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

	// --- Confirmation ---
	if (confirm_dialog.open_request) {
		confirm_dialog.open_request = false;
		ImGui::OpenPopup("###Confirm");
	}
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal((confirm_dialog.title + "###Confirm").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::PushTextWrapPos(460);
		ImGui::TextUnformatted(confirm_dialog.message.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		for (size_t i = 0; i < confirm_dialog.buttons.size(); i++) {
			if (i > 0) {
				ImGui::SameLine();
			}
			if (ImGui::Button(confirm_dialog.buttons[i].first.c_str(), ImVec2(150, 0))) {
				std::function<void()> action = confirm_dialog.buttons[i].second;
				if (action) {
					_defer(action);
				}
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}

	// --- Sélecteur de fichiers du projet ---
	if (file_dialog.open_request) {
		file_dialog.open_request = false;
		_scan_filesystem();
		ImGui::OpenPopup("###FileDialog");
	}
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(600, 460), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal((file_dialog.title + "###FileDialog").c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
		String chosen;
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::IsWindowAppearing() && !file_dialog.save_mode) {
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::InputTextWithHint("##filter", "Filtrer...", &file_dialog.filter);
		const float footer = ImGui::GetFrameHeightWithSpacing() * (file_dialog.save_mode ? 2.0f : 1.0f) + 8;
		ImGui::BeginChild("##list", ImVec2(0, -footer), ImGuiChildFlags_Borders);
		int shown = 0;
		for (const String &path : fs_files) {
			if (!_extension_matches(path, file_dialog.extensions)) {
				continue;
			}
			if (!file_dialog.filter.empty() && !str_match_nocase(path, file_dialog.filter)) {
				continue;
			}
			shown++;
			if (ImGui::Selectable(path.c_str(), file_dialog.save_mode && path == file_dialog.filename, ImGuiSelectableFlags_AllowDoubleClick)) {
				if (file_dialog.save_mode) {
					file_dialog.filename = path;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						chosen = path;
					}
				} else {
					chosen = path;
				}
			}
		}
		if (shown == 0) {
			ImGui::TextDisabled("Aucun fichier %s dans le projet.", file_dialog.extensions.empty() ? "" : ("." + str_join(file_dialog.extensions, ", .")).c_str());
		}
		ImGui::EndChild();
		if (file_dialog.save_mode) {
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::IsWindowAppearing()) {
				ImGui::SetKeyboardFocusHere();
			}
			if (ImGui::InputTextWithHint("##filename", "res://dossier/nom", &file_dialog.filename, ImGuiInputTextFlags_EnterReturnsTrue)) {
				chosen = file_dialog.filename;
			}
			if (ImGui::Button("Valider", ImVec2(120, 0))) {
				chosen = file_dialog.filename;
			}
			ImGui::SameLine();
		}
		if (ImGui::Button("Annuler", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		if (!chosen.empty()) {
			String path = str_strip(chosen);
			if (!str_begins_with(path, "res://")) {
				path = "res://" + path;
			}
			const bool has_name = !path_get_file(path).empty() && path != "res://";
			if (has_name) {
				if (file_dialog.save_mode && !file_dialog.extensions.empty() && !_extension_matches(path, file_dialog.extensions)) {
					path += "." + file_dialog.extensions[0];
				}
				std::function<void(const String &)> callback = file_dialog.callback;
				if (callback) {
					_defer([callback, path]() { callback(path); });
				}
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}

	_draw_create_node_dialog();
	_draw_connect_dialog();
	_draw_project_settings();
	_draw_about();
}

void EditorNode::_draw_create_class_tree(const String &p_class, const String &p_filter) {
	std::vector<String> inheriters;
	ClassDB::get_direct_inheriters(p_class, &inheriters);
	std::sort(inheriters.begin(), inheriters.end());
	const bool can_create = ClassDB::can_instantiate(p_class);

	if (!p_filter.empty()) {
		// Liste à plat des classes correspondant au filtre.
		if (can_create && str_match_nocase(p_class, p_filter)) {
			ImGui::PushStyleColor(ImGuiCol_Text, _class_color(p_class));
			if (ImGui::Selectable(p_class.c_str(), create_dialog.selected == p_class, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups)) {
				create_dialog.selected = p_class;
			}
			ImGui::PopStyleColor();
		}
		for (const String &child : inheriters) {
			_draw_create_class_tree(child, p_filter);
		}
		return;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (inheriters.empty()) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (create_dialog.selected == p_class) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	ImVec4 color = _class_color(p_class);
	if (!can_create) {
		color.w = 0.5f;
	}
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	const bool open = ImGui::TreeNodeEx(p_class.c_str(), flags);
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && can_create) {
		create_dialog.selected = p_class;
	}
	if (open) {
		for (const String &child : inheriters) {
			_draw_create_class_tree(child, p_filter);
		}
		ImGui::TreePop();
	}
}

void EditorNode::_draw_create_node_dialog() {
	if (create_dialog.open_request) {
		create_dialog.open_request = false;
		create_dialog.filter.clear();
		if (create_dialog.selected.empty()) {
			create_dialog.selected = "Node2D";
		}
		ImGui::OpenPopup("Créer un nœud###CreateNode");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(440, 540), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal("Créer un nœud###CreateNode", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	bool create = false;
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere();
	}
	if (ImGui::InputTextWithHint("##filter", "Rechercher un type de nœud...", &create_dialog.filter, ImGuiInputTextFlags_EnterReturnsTrue)) {
		create = true;
	}
	ImGui::BeginChild("##classes", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8), ImGuiChildFlags_Borders);
	_draw_create_class_tree("Node", create_dialog.filter);
	if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ClassDB::can_instantiate(create_dialog.selected)) {
		create = true;
	}
	ImGui::EndChild();

	// Avec un filtre, la sélection suit le premier résultat si l'ancienne ne correspond plus.
	if (!create_dialog.filter.empty() && !str_match_nocase(create_dialog.selected, create_dialog.filter)) {
		std::vector<String> classes;
		ClassDB::get_class_list(&classes);
		std::sort(classes.begin(), classes.end());
		for (const String &cls : classes) {
			if (ClassDB::is_parent_class(cls, "Node") && ClassDB::can_instantiate(cls) && str_match_nocase(cls, create_dialog.filter)) {
				create_dialog.selected = cls;
				break;
			}
		}
	}

	ImGui::BeginDisabled(!ClassDB::can_instantiate(create_dialog.selected));
	if (ImGui::Button(("Créer " + create_dialog.selected).c_str(), ImVec2(220, 0))) {
		create = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Annuler", ImVec2(120, 0))) {
		ImGui::CloseCurrentPopup();
	}
	if (create && ClassDB::can_instantiate(create_dialog.selected) && ClassDB::is_parent_class(create_dialog.selected, "Node")) {
		const String type = create_dialog.selected;
		const ObjectID parent_id = create_dialog.parent_id;
		const bool as_root = create_dialog.as_root;
		_defer([this, type, parent_id, as_root]() {
			_create_node(type, as_root ? nullptr : Object::cast_to<Node>(ObjectDB::get_instance(parent_id)));
		});
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void EditorNode::_draw_about() {
	if (about_open) {
		about_open = false;
		ImGui::OpenPopup("À propos###About");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("À propos###About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		_draw_brand_header(72.0f);
		ImGui::TextUnformatted("Moteur de jeu 2D et 3D avec éditeur intégré, créé par Metoushela.");
		ImGui::TextDisabled("Son architecture s'inspire de celle de Godot Engine.");
		ImGui::Spacing();
		ImGui::TextUnformatted("Bibliothèques tierces :");
		ImGui::BulletText("SDL2 (zlib)");
		ImGui::BulletText("Dear ImGui %s (MIT)", ImGui::GetVersion());
		ImGui::BulletText("stb_image, stb_truetype (domaine public / MIT)");
		ImGui::BulletText("Police Roboto (Apache 2.0)");
		ImGui::BulletText("Police Poppins, tracés du logo (SIL OFL 1.1)");
		ImGui::Spacing();
		if (ImGui::Button("Fermer", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
