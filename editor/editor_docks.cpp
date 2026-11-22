// Docks « Scène », « Fichiers », « Sortie » et boîtes de dialogue communes.
#include "editor_node.h"

#include "core/config/engine.h"
#include "core/debug_monitor.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/string/ustring.h"
#include "scene/main/canvas_item.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/packed_scene.h"
#include "core/object/script_language.h"

#include "editor_ui.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace {

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
	if (!ImGui::Begin(ICON_DOCK_SCENE "  Scène###SceneDock")) {
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
	// Dock étroit : les deux boutons ne gardent que leur icône (l'infobulle dit le reste).
	const bool compact_buttons = ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(ICON_PLUS "  Nœud" ICON_FILE_SCENE "  Instancier").x + ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x;
	if (ImGui::Button(compact_buttons ? ICON_PLUS "###add_node" : ICON_PLUS "  Nœud###add_node")) {
		create_dialog.open_request = true;
		create_dialog.as_root = false;
		create_dialog.parent_id = (selected && _is_editable(selected) ? selected : scene_root)->get_instance_id();
	}
	ImGui::SetItemTooltip("Ajouter un nœud enfant au nœud sélectionné");
	ImGui::SameLine();
	if (ImGui::Button(compact_buttons ? ICON_FILE_SCENE "###instance" : ICON_FILE_SCENE "  Instancier###instance")) {
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
		// Le libellé est dessiné par-dessus un nœud d'arbre vide : icône à la couleur de la
		// famille (2D, 3D, interface), nom à la couleur du texte.
		const ImVec2 row = ImGui::GetCursorScreenPos();
		open = ImGui::TreeNodeEx("##node", flags, "%s", "");
		const float alpha = hidden ? 0.45f : 1.0f;
		EditorUI::tree_label(row, _class_icon(p_node->get_class()), EditorUI::u32(_class_color(p_node->get_class()), alpha), p_node->get_name().c_str(), EditorUI::u32(palette.text, alpha));

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

		// Pastilles à droite : scène instanciée, script attaché, nœud masqué.
		String badges;
		if (is_instance) {
			badges += ICON_FILE_SCENE " ";
		}
		if (has_script) {
			badges += ICON_FILE_SCRIPT " ";
		}
		if (hidden) {
			badges += ICON_EYE_OFF " ";
		}
		if (!badges.empty()) {
			const float width = ImGui::CalcTextSize(badges.c_str()).x;
			const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x + ImGui::GetScrollX();
			const float label_end = row.x + ImGui::GetFontSize() * 2.6f + ImGui::GetStyle().FramePadding.x * 2 + ImGui::CalcTextSize(p_node->get_name().c_str()).x;
			ImGui::GetWindowDrawList()->AddText(ImVec2(std::max(right - width, label_end + ES(8)), row.y), EditorUI::u32(palette.text_dim), badges.c_str());
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

const EditorNode::FsEntry *EditorNode::_find_fs_entry(const FsEntry &p_entry, const String &p_path) const {
	if (p_entry.path == p_path) {
		return &p_entry;
	}
	for (const FsEntry &child : p_entry.children) {
		if (child.is_dir && (child.path == p_path || str_begins_with(p_path, child.path + "/"))) {
			return _find_fs_entry(child, p_path);
		}
	}
	return nullptr;
}

void EditorNode::_fs_dir_menu(const String &p_path) {
	if (ImGui::MenuItem(ICON_FOLDER_PLUS "  Nouveau dossier...")) {
		fs_new_kind = 1;
		fs_new_dir = p_path;
		fs_new_name = "nouveau_dossier";
	}
	if (ImGui::MenuItem(ICON_FILE_PLUS "  Nouveau script...")) {
		fs_new_kind = 2;
		fs_new_dir = p_path;
		fs_new_name = "nouveau_script.mx";
	}
	if (ImGui::MenuItem(ICON_REFRESH "  Actualiser")) {
		fs_dirty = true;
	}
}

// Comportement commun à un fichier, dans l'arbre comme dans la grille : sélection, ouverture
// au double-clic, glisser-déposer, menu contextuel, aperçu au survol.
void EditorNode::_fs_file_interactions(const String &p_path) {
	const String ext = path_get_extension(p_path);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
		fs_selected = p_path;
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		const String path = p_path;
		_defer([this, path]() { _open_file(path); });
	}
	if (ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload("MEX_FILE", p_path.c_str(), p_path.size() + 1);
		ImGui::TextColored(EditorUI::col(_file_color(p_path)), "%s", _file_icon(p_path));
		ImGui::SameLine();
		ImGui::TextUnformatted(p_path.c_str());
		ImGui::EndDragDropSource();
	} else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay) && pref_thumbnails && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		_draw_preview_tooltip(p_path);
	}
	if (ImGui::BeginPopupContextItem("##ctx")) {
		const String path = p_path;
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

void EditorNode::_draw_fs_entry(const FsEntry &p_entry) {
	using EditorUI::col;
	ImGui::PushID(p_entry.path.c_str());
	if (p_entry.is_dir) {
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_AllowOverlap;
		if (p_entry.path == "res://") {
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		const bool open = ImGui::TreeNodeEx("##dir", flags, "%s", "");
		if (ImGui::IsItemClicked()) {
			fs_current_dir = p_entry.path;
		}
		if (ImGui::BeginPopupContextItem("##ctx")) {
			_fs_dir_menu(p_entry.path);
			ImGui::EndPopup();
		}
		ImGui::SameLine(0, 0);
		ImGui::TextColored(col(palette.folder), "%s", open ? ICON_FOLDER_OPEN : ICON_FOLDER);
		ImGui::SameLine();
		ImGui::TextUnformatted(p_entry.name.c_str());
		if (open) {
			for (const FsEntry &child : p_entry.children) {
				_draw_fs_entry(child);
			}
			ImGui::TreePop();
		}
	} else {
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
		if (fs_selected == p_entry.path) {
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		ImGui::TreeNodeEx("##file", flags, "%s", "");
		_fs_file_interactions(p_entry.path);
		ImGui::SameLine(0, 0);
		// Une miniature à la place de l'icône quand le fichier a une image d'aperçu.
		const float side = ImGui::GetTextLineHeight();
		const FilePreview *preview = _get_preview(p_entry.path);
		if (_has_preview_image(preview)) {
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(side + ES(2), side));
			_draw_preview(p_entry.path, Vector2(pos.x, pos.y), Vector2(pos.x + side, pos.y + side));
		} else {
			ImGui::TextColored(col(_file_color(p_entry.path)), "%s", _file_icon(p_entry.path));
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(p_entry.name.c_str());
	}
	ImGui::PopID();
}

// Grille de miniatures du dossier courant.
void EditorNode::_draw_fs_grid() {
	using EditorUI::col;
	using EditorUI::u32;
	const ImGuiStyle &style = ImGui::GetStyle();
	const FsEntry *dir = _find_fs_entry(fs_root, fs_current_dir);
	if (!dir || !dir->is_dir) {
		fs_current_dir = "res://";
		dir = &fs_root;
	}
	// --- Fil d'Ariane ---
	ImGui::BeginDisabled(fs_current_dir == "res://");
	if (ImGui::Button(ICON_ARROW_UP)) {
		String parent = path_get_base_dir(fs_current_dir);
		fs_current_dir = (parent.empty() || parent == "res:" || parent == "res:/") ? String("res://") : parent;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Dossier parent");
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored(col(palette.folder), ICON_FOLDER_OPEN);
	ImGui::SameLine();
	ImGui::TextUnformatted(fs_current_dir.c_str());

	ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None);
	if (ImGui::BeginPopupContextWindow("##gridctx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
		_fs_dir_menu(fs_current_dir);
		ImGui::EndPopup();
	}
	const float thumb = ES(pref_thumbnail_size);
	const float cell_w = thumb + ES(18);
	const float cell_h = thumb + ImGui::GetTextLineHeight() + ES(16);
	const float avail = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1, (int)((avail + style.ItemSpacing.x) / (cell_w + style.ItemSpacing.x)));
	ImDrawList *dl = ImGui::GetWindowDrawList();
	String enter_dir;
	int column = 0;
	if (dir->children.empty()) {
		EditorUI::hint("Ce dossier est vide.");
	}
	for (const FsEntry &entry : dir->children) {
		ImGui::PushID(entry.path.c_str());
		if (column > 0) {
			ImGui::SameLine();
		}
		column = (column + 1) % columns;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##cell", ImVec2(cell_w, cell_h));
		const bool hovered = ImGui::IsItemHovered();
		const bool selected = !entry.is_dir && fs_selected == entry.path;
		if (entry.is_dir) {
			if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				enter_dir = entry.path;
			}
			if (ImGui::BeginPopupContextItem("##ctx")) {
				_fs_dir_menu(entry.path);
				ImGui::EndPopup();
			}
			ImGui::SetItemTooltip("%s\nDouble-clic pour ouvrir le dossier", entry.path.c_str());
		} else {
			_fs_file_interactions(entry.path);
		}
		if (selected) {
			dl->AddRectFilled(pos, ImVec2(pos.x + cell_w, pos.y + cell_h), u32(palette.accent, 0.20f), style.FrameRounding);
			dl->AddRect(pos, ImVec2(pos.x + cell_w, pos.y + cell_h), u32(palette.accent, 0.8f), style.FrameRounding);
		} else if (hovered) {
			dl->AddRectFilled(pos, ImVec2(pos.x + cell_w, pos.y + cell_h), u32(palette.bg_light), style.FrameRounding);
		}
		const ImVec2 box(pos.x + ES(9), pos.y + ES(6));
		if (entry.is_dir || !_draw_preview(entry.path, Vector2(box.x, box.y), Vector2(box.x + thumb, box.y + thumb))) {
			// Pas d'image d'aperçu : une grande icône.
			const char *icon = entry.is_dir ? ICON_FOLDER : _file_icon(entry.path);
			const float icon_size = thumb * 0.62f;
			ImFont *font = ImGui::GetFont();
			const ImVec2 size = font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon);
			dl->AddText(font, icon_size, ImVec2(box.x + (thumb - size.x) * 0.5f, box.y + (thumb - size.y) * 0.5f), u32(entry.is_dir ? palette.folder : _file_color(entry.path)), icon);
		}
		// Nom, centré, coupé s'il dépasse.
		const float name_w = ImGui::CalcTextSize(entry.name.c_str()).x;
		const float text_y = pos.y + ES(6) + thumb + ES(4);
		const ImVec4 clip(pos.x + ES(3), text_y, pos.x + cell_w - ES(3), text_y + ImGui::GetTextLineHeight());
		const float text_x = name_w <= cell_w - ES(6) ? pos.x + (cell_w - name_w) * 0.5f : pos.x + ES(3);
		dl->AddText(nullptr, 0.0f, ImVec2(text_x, text_y), u32(palette.text), entry.name.c_str(), nullptr, 0.0f, &clip);
		ImGui::PopID();
	}
	ImGui::EndChild();
	if (!enter_dir.empty()) {
		fs_current_dir = enter_dir;
	}
}

// Fiche du fichier sélectionné, en bas du dock : grand aperçu et informations.
void EditorNode::_draw_fs_preview_panel() {
	using EditorUI::col;
	using EditorUI::u32;
	const float side = ES(104);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImGui::Dummy(ImVec2(side, side));
	dl->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), u32(palette.frame), ImGui::GetStyle().FrameRounding);
	const float margin = ES(4);
	if (!_draw_preview(fs_selected, Vector2(pos.x + margin, pos.y + margin), Vector2(pos.x + side - margin, pos.y + side - margin))) {
		const char *icon = _file_icon(fs_selected);
		const float icon_size = side * 0.5f;
		ImFont *font = ImGui::GetFont();
		const ImVec2 size = font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon);
		dl->AddText(font, icon_size, ImVec2(pos.x + (side - size.x) * 0.5f, pos.y + (side - size.y) * 0.5f), u32(_file_color(fs_selected)), icon);
	}
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted(path_get_file(fs_selected).c_str());
	static const char *kinds[] = { "Fichier", "Image", "Maillage 3D", "Scène", "Son", "Texte" };
	const int kind = _get_preview_kind(fs_selected);
	ImGui::TextColored(col(_file_color(fs_selected)), "%s", path_get_extension(fs_selected) == "mx" ? "Script MexScript" : kinds[kind]);
	const FilePreview *preview = _get_preview(fs_selected);
	if (preview && preview->ready && !preview->info.empty()) {
		ImGui::TextDisabled("%s", preview->info.c_str());
	}
	ImGui::TextDisabled("%s", fs_selected.c_str());
	ImGui::PopTextWrapPos();
	ImGui::EndGroup();
}

void EditorNode::_draw_filesystem_dock() {
	if (!ImGui::Begin(ICON_DOCK_FILES "  Fichiers###FilesDock")) {
		ImGui::End();
		return;
	}
	// --- Barre d'outils ---
	if (ImGui::Button(ICON_REFRESH)) {
		fs_dirty = true;
		_clear_previews();
	}
	ImGui::SetItemTooltip("Actualiser la liste et les aperçus");
	ImGui::SameLine();
	if (ImGui::Button(ICON_FILE_PLUS)) {
		fs_new_kind = 2;
		fs_new_dir = pref_files_grid ? fs_current_dir : String("res://");
		fs_new_name = "nouveau_script.mx";
	}
	ImGui::SetItemTooltip("Nouveau script");
	ImGui::SameLine();
	if (ImGui::Button(ICON_FOLDER_PLUS)) {
		fs_new_kind = 1;
		fs_new_dir = pref_files_grid ? fs_current_dir : String("res://");
		fs_new_name = "nouveau_dossier";
	}
	ImGui::SetItemTooltip("Nouveau dossier");
	ImGui::SameLine();
	if (ImGui::Button(pref_files_grid ? ICON_DOCK_SCENE : ICON_NODE_CONTAINER)) {
		pref_files_grid = !pref_files_grid;
		_save_prefs();
	}
	ImGui::SetItemTooltip("%s", pref_files_grid ? "Afficher l'arborescence" : "Afficher une grille de miniatures");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	{
		// L'indication se raccourcit avec le champ plutôt que d'être coupée.
		const float room = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2.0f;
		const char *hint = ICON_SEARCH "  Filtrer les fichiers";
		if (ImGui::CalcTextSize(hint).x > room) {
			hint = ICON_SEARCH "  Filtrer";
		}
		if (ImGui::CalcTextSize(hint).x > room) {
			hint = ICON_SEARCH;
		}
		ImGui::InputTextWithHint("##fsfilter", hint, &fs_filter);
	}
	ImGui::SetItemTooltip("Filtrer les fichiers par nom");

	// --- Liste, puis fiche du fichier sélectionné ---
	const bool show_panel = !fs_selected.empty() && std::find(fs_files.begin(), fs_files.end(), fs_selected) != fs_files.end();
	const float panel_h = show_panel ? ES(104) + ImGui::GetStyle().ItemSpacing.y * 2 + 1.0f : 0.0f;
	ImGui::BeginChild("##files", ImVec2(0, -panel_h), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	const String filter = str_to_lower(str_strip(fs_filter));
	if (!filter.empty()) {
		// Recherche : liste à plat des fichiers dont le chemin contient le texte.
		int shown = 0;
		for (const String &path : fs_files) {
			if (str_to_lower(path).find(filter) == String::npos) {
				continue;
			}
			shown++;
			ImGui::PushID(path.c_str());
			ImGui::TreeNodeEx("##file", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap | (fs_selected == path ? ImGuiTreeNodeFlags_Selected : 0), "%s", "");
			_fs_file_interactions(path);
			ImGui::SameLine(0, 0);
			ImGui::TextColored(EditorUI::col(_file_color(path)), "%s", _file_icon(path));
			ImGui::SameLine();
			ImGui::TextUnformatted(path.substr(6).c_str());
			ImGui::PopID();
		}
		if (shown == 0) {
			EditorUI::hint("Aucun fichier ne correspond.");
		}
	} else if (pref_files_grid) {
		_draw_fs_grid();
	} else {
		_draw_fs_entry(fs_root);
	}
	ImGui::EndChild();
	if (show_panel) {
		ImGui::Separator();
		_draw_fs_preview_panel();
	}
	ImGui::End();

	// Création d'un dossier ou d'un script.
	if (fs_new_kind != 0) {
		ImGui::OpenPopup("###FsNew");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	const char *title = fs_new_kind == 1 ? "Nouveau dossier###FsNew" : "Nouveau script###FsNew";
	if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Dans : %s", fs_new_dir.c_str());
		ImGui::SetNextItemWidth(ES(320));
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		const bool entered = ImGui::InputText("##name", &fs_new_name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (ImGui::Button("Créer", ImVec2(ES(120), 0)) || entered) {
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
		if (ImGui::Button("Annuler", ImVec2(ES(120), 0))) {
			fs_new_kind = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}


// ---------------------------------------------------------------------------
// Dock « Sortie »
// ---------------------------------------------------------------------------
// Profileur : temps passé dans chaque fonction de script du jeu en cours (les courbes
// générales sont dans le dock « Moniteur »).
// Fenêtre placée dans le même groupe d'onglets que la sortie.
static void _draw_profiler_window(ImGuiID p_dock_id, bool p_game_running) {
	if (p_dock_id) {
		ImGui::SetNextWindowDockID(p_dock_id, ImGuiCond_FirstUseEver);
	}
	if (!ImGui::Begin(ICON_NODE_PROGRESS "  Profileur###Profiler")) {
		ImGui::End();
		return;
	}
	Performance *performance = Performance::get_singleton();
	static Array profile;
	static double next_refresh = 0;
	const double now = ImGui::GetTime();
	if (now >= next_refresh) {
		// Rafraîchi deux fois par seconde : des chiffres qui changent à chaque image sont illisibles.
		next_refresh = now + 0.5;
		profile = performance->get_script_profile(false);
	}
	bool profiling = performance->is_script_profiling();
	if (ImGui::Checkbox("Mesurer les scripts", &profiling)) {
		performance->set_script_profiling(profiling);
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_TRASH "  Remettre à zéro")) {
		performance->get_script_profile(true);
		profile = Array();
	}
	ImGui::SameLine();
	ImGui::TextDisabled(p_game_running ? "Jeu en cours" : "Lancez le jeu pour obtenir des mesures");
	ImGui::Separator();

	ImGui::BeginChild("##fonctions", ImVec2(0, 0));
	if (profile.is_empty()) {
		ImGui::TextDisabled(profiling ? "Aucune fonction de script appelée pour l'instant." : "Cochez « Mesurer les scripts », puis lancez le jeu.");
	} else if (ImGui::BeginTable("##profil", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Fonction");
		ImGui::TableSetupColumn("Script");
		ImGui::TableSetupColumn("Appels");
		ImGui::TableSetupColumn("Propre (ms)");
		ImGui::TableSetupColumn("Total (ms)");
		ImGui::TableHeadersRow();
		for (int i = 0; i < profile.size(); i++) {
			const Dictionary entry = profile[i].as_dictionary();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(entry.get(Variant("function"), Variant("")).as_string().c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(entry.get(Variant("source"), Variant("")).as_string().c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%lld", (long long)entry.get(Variant("calls"), Variant((int64_t)0)).as_int());
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", (double)entry.get(Variant("self_usec"), Variant((int64_t)0)).as_int() / 1000.0);
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", (double)entry.get(Variant("total_usec"), Variant((int64_t)0)).as_int() / 1000.0);
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();
	ImGui::End();
}

void EditorNode::_draw_console_dock() {
	static ImGuiID console_dock_id = 0;
	if (focus_console_frames > 0 && --focus_console_frames == 0) {
		ImGui::SetNextWindowFocus();
		focus_viewport = true; // la vue 2D (dessinée ensuite) reprend le focus clavier
	}
	if (!ImGui::Begin(ICON_DOCK_OUTPUT "  Sortie###Console")) {
		console_dock_id = ImGui::GetWindowDockID();
		ImGui::End();
		_draw_profiler_window(console_dock_id, game.is_running());
		return;
	}
	console_dock_id = ImGui::GetWindowDockID();
	if (ImGui::Button(ICON_TRASH "  Effacer")) {
		console_lines.clear();
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_COPY "  Copier")) {
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
			ImVec4 color = EditorUI::col(palette.text);
			if (line.level == PRINT_ERROR) {
				color = EditorUI::col(palette.error);
			} else if (line.level == PRINT_WARNING) {
				color = EditorUI::col(palette.warning);
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
	_draw_profiler_window(console_dock_id, game.is_running());
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
		ImGui::PushTextWrapPos(ES(460));
		ImGui::TextUnformatted(confirm_dialog.message.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		for (size_t i = 0; i < confirm_dialog.buttons.size(); i++) {
			if (i > 0) {
				ImGui::SameLine();
			}
			if (ImGui::Button(confirm_dialog.buttons[i].first.c_str(), ImVec2(ES(150), 0))) {
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
	ImGui::SetNextWindowSize(EditorUI::dialog_size(600, 460), ImGuiCond_Appearing);
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
			if (ImGui::Button("Valider", ImVec2(ES(120), 0))) {
				chosen = file_dialog.filename;
			}
			ImGui::SameLine();
		}
		if (ImGui::Button("Annuler", ImVec2(ES(120), 0))) {
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
	_draw_preferences();
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
			const ImVec2 row = ImGui::GetCursorScreenPos();
			if (ImGui::Selectable(("##" + p_class).c_str(), create_dialog.selected == p_class, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups)) {
				create_dialog.selected = p_class;
			}
			EditorUI::icon_label(row, _class_icon(p_class), EditorUI::u32(_class_color(p_class)), p_class.c_str(), EditorUI::u32(palette.text));
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
	const ImVec2 row = ImGui::GetCursorScreenPos();
	const bool open = ImGui::TreeNodeEx(p_class.c_str(), flags, "%s", "");
	const float alpha = can_create ? 1.0f : 0.5f;
	EditorUI::tree_label(row, _class_icon(p_class), EditorUI::u32(_class_color(p_class), alpha), p_class.c_str(), EditorUI::u32(palette.text, alpha));
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
	ImGui::SetNextWindowSize(EditorUI::dialog_size(440, 540), ImGuiCond_Appearing);
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
	if (ImGui::Button(("Créer " + create_dialog.selected).c_str(), ImVec2(ES(220), 0))) {
		create = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Annuler", ImVec2(ES(120), 0))) {
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
		_draw_brand_header(ES(72), true);
		ImGui::TextUnformatted("Moteur de jeu 2D et 3D avec éditeur intégré, créé par Metoushela.");
		ImGui::TextDisabled("Son architecture s'inspire de celle de Godot Engine.");
		ImGui::Spacing();
		ImGui::TextUnformatted("Bibliothèques tierces :");
		ImGui::BulletText("SDL2 (zlib)");
		ImGui::BulletText("Dear ImGui %s (MIT)", ImGui::GetVersion());
		ImGui::BulletText("stb_image, stb_truetype (domaine public / MIT)");
		ImGui::BulletText("Police Roboto (Apache 2.0)");
		ImGui::BulletText("Icônes Lucide (ISC)");
		ImGui::Spacing();
		if (ImGui::Button("Fermer", ImVec2(ES(120), 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
