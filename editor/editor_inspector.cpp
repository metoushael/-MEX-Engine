// Inspecteur : propriétés, signaux et groupes du nœud sélectionné.
#include "editor_node.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/variant_parser.h"
#include "core/object/script_language.h"
#include "core/string/ustring.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

namespace {

void _parse_range(const String &p_hint, double &r_min, double &r_max, double &r_step) {
	std::vector<String> parts = str_split(p_hint, ",");
	if (parts.size() >= 2) {
		r_min = strtod(parts[0].c_str(), nullptr);
		r_max = strtod(parts[1].c_str(), nullptr);
	}
	if (parts.size() >= 3) {
		r_step = strtod(parts[2].c_str(), nullptr);
	}
}

String _snake_case(const String &p_name) {
	String out;
	for (size_t i = 0; i < p_name.size(); i++) {
		const char c = p_name[i];
		if (c >= 'A' && c <= 'Z') {
			if (i > 0 && out.back() != '_' && !(p_name[i - 1] >= 'A' && p_name[i - 1] <= 'Z')) {
				out += '_';
			}
			out += (char)(c - 'A' + 'a');
		} else if (c == ' ' || c == '-') {
			out += '_';
		} else {
			out += c;
		}
	}
	return out;
}

struct Section {
	String name;
	std::vector<PropertyInfo> properties;
};

} // namespace

// ---------------------------------------------------------------------------
// Dock
// ---------------------------------------------------------------------------
void EditorNode::_draw_inspector_dock() {
	if (!ImGui::Begin("Inspecteur###Inspector")) {
		ImGui::End();
		return;
	}
	Node *node = _get_selected();
	if (!node) {
		ImGui::TextDisabled("Sélectionnez un nœud pour modifier ses propriétés.");
		ImGui::End();
		return;
	}

	// En-tête : type + nom.
	ImGui::TextDisabled("%s", node->get_class().c_str());
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	std::string name = node->get_name();
	if (ImGui::InputText("##node_name", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
		const String new_name = str_strip(name);
		if (!new_name.empty() && new_name != node->get_name()) {
			node->set_name(new_name);
			_mark_modified();
		}
	}
	if (node != scene_root && !node->get_scene_file_path().empty()) {
		ImGui::TextDisabled("Instance de %s", node->get_scene_file_path().c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Ouvrir")) {
			const String path = node->get_scene_file_path();
			_check_unsaved_then([this, path]() { _open_scene(path); });
		}
	}
	ImGui::Separator();

	if (ImGui::BeginTabBar("##inspector_tabs")) {
		if (ImGui::BeginTabItem("Propriétés")) {
			ImGui::BeginChild("##props", ImVec2(0, 0));
			_draw_object_properties(node, true);
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Signaux")) {
			ImGui::BeginChild("##signals", ImVec2(0, 0));
			_draw_node_signals(node);
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Groupes")) {
			_draw_node_groups(node);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void EditorNode::_draw_object_properties(Object *p_object, bool p_is_node) {
	std::vector<PropertyInfo> list;
	p_object->get_property_list(&list);

	// Regroupe par catégorie (classe), puis affiche la plus spécifique en premier.
	std::vector<Section> sections;
	for (const PropertyInfo &pi : list) {
		if (pi.usage & PROPERTY_USAGE_CATEGORY) {
			sections.push_back({ pi.name, {} });
			continue;
		}
		if (sections.empty()) {
			sections.push_back({ p_object->get_class(), {} });
		}
		sections.back().properties.push_back(pi);
	}
	std::reverse(sections.begin(), sections.end());

	Ref<Script> script = Ref<Script>(Object::cast_to<Script>(p_object->get_script().as_object()));
	const ObjectID object_id = p_object->get_instance_id();

	for (const Section &section : sections) {
		bool has_visible = false;
		for (const PropertyInfo &pi : section.properties) {
			if ((pi.usage & PROPERTY_USAGE_EDITOR) && !(pi.usage & PROPERTY_USAGE_GROUP)) {
				has_visible = true;
				break;
			}
		}
		if (!has_visible) {
			continue;
		}
		if (p_is_node) {
			if (!ImGui::CollapsingHeader(section.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				continue;
			}
		}
		const float label_width = std::max(110.0f, ImGui::GetContentRegionAvail().x * 0.40f);
		for (const PropertyInfo &pi : section.properties) {
			if (pi.usage & PROPERTY_USAGE_GROUP) {
				ImGui::SeparatorText(pi.name.c_str());
				continue;
			}
			if (!(pi.usage & PROPERTY_USAGE_EDITOR)) {
				continue;
			}
			// Un objet peut être remplacé pendant l'édition (annulation...) : on vérifie sa validité.
			if (ObjectDB::get_instance(object_id) != p_object) {
				return;
			}
			ImGui::PushID(pi.name.c_str());
			Variant value = p_object->get(pi.name);

			// Valeur par défaut (pour le bouton de réinitialisation).
			bool has_default = false;
			Variant default_value;
			if (pi.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) {
				has_default = script.is_valid() && script->get_property_default_value(pi.name, default_value);
			} else {
				default_value = ClassDB::class_get_default_property_value(p_object->get_class(), pi.name, &has_default);
			}
			const bool is_modified = has_default && value.get_type() != Variant::OBJECT && value != default_value;

			ImGui::AlignTextToFramePadding();
			const String label = str_capitalize(pi.name);
			if (is_modified) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
			}
			{
				// Le libellé est coupé à la largeur de sa colonne (le nom complet est dans l'infobulle).
				const ImVec2 label_pos = ImGui::GetCursorScreenPos();
				ImGui::PushClipRect(label_pos, ImVec2(label_pos.x + label_width - ImGui::GetCursorPosX() - 6, label_pos.y + ImGui::GetFrameHeight()), true);
				ImGui::TextUnformatted(label.c_str());
				ImGui::PopClipRect();
			}
			if (is_modified) {
				ImGui::PopStyleColor();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
				ImGui::SetTooltip("%s : %s%s", pi.name.c_str(), Variant::get_type_name(pi.type).c_str(), is_modified ? "\nClic droit : réinitialiser" : "");
			}
			if (is_modified && ImGui::BeginPopupContextItem("##reset")) {
				if (ImGui::MenuItem("Réinitialiser à la valeur par défaut")) {
					p_object->set(pi.name, default_value);
					_mark_modified();
				}
				ImGui::EndPopup();
			}
			ImGui::SameLine(label_width);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (_edit_property(p_object, pi, value)) {
				if (ObjectDB::get_instance(object_id) == p_object) {
					p_object->set(pi.name, value);
				}
				_mark_modified();
			}
			ImGui::PopID();
		}
	}
}

// ---------------------------------------------------------------------------
// Éditeurs de valeurs
// ---------------------------------------------------------------------------
bool EditorNode::_edit_property(Object *p_object, const PropertyInfo &p_info, Variant &r_value) {
	Variant::Type type = p_info.type;
	if (type == Variant::NIL) {
		type = r_value.get_type();
	}
	switch (type) {
		case Variant::BOOL: {
			bool v = r_value.as_bool();
			if (ImGui::Checkbox("##v", &v)) {
				r_value = Variant(v);
				return true;
			}
			return false;
		}
		case Variant::INT: {
			int v = (int)r_value.as_int();
			if (p_info.hint == PROPERTY_HINT_ENUM) {
				std::vector<String> options = str_split(p_info.hint_string, ",");
				const String preview = v >= 0 && v < (int)options.size() ? str_strip(options[v]) : itos(v);
				bool changed = false;
				if (ImGui::BeginCombo("##v", preview.c_str())) {
					for (int i = 0; i < (int)options.size(); i++) {
						if (ImGui::Selectable(str_strip(options[i]).c_str(), i == v)) {
							r_value = Variant((int64_t)i);
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				return changed;
			}
			if (p_info.hint == PROPERTY_HINT_RANGE) {
				double min = 0, max = 100, step = 1;
				_parse_range(p_info.hint_string, min, max, step);
				if (ImGui::DragInt("##v", &v, (float)std::max(1.0, step) * 0.25f, (int)min, (int)max, "%d", ImGuiSliderFlags_AlwaysClamp)) {
					r_value = Variant((int64_t)v);
					return true;
				}
				return false;
			}
			if (ImGui::DragInt("##v", &v, 0.25f)) {
				r_value = Variant((int64_t)v);
				return true;
			}
			return false;
		}
		case Variant::FLOAT: {
			float v = (float)r_value.as_float();
			if (p_info.hint == PROPERTY_HINT_RADIANS) {
				float degrees = (float)Math::rad_to_deg((double)v);
				if (ImGui::DragFloat("##v", &degrees, 0.5f, 0.0f, 0.0f, "%.1f°")) {
					r_value = Variant(Math::deg_to_rad((double)degrees));
					return true;
				}
				return false;
			}
			if (p_info.hint == PROPERTY_HINT_RANGE) {
				double min = 0, max = 1, step = 0.01;
				_parse_range(p_info.hint_string, min, max, step);
				if (ImGui::DragFloat("##v", &v, (float)std::max(step, (max - min) / 400.0), (float)min, (float)max, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
					r_value = Variant((double)v);
					return true;
				}
				return false;
			}
			if (ImGui::DragFloat("##v", &v, std::max(0.01f, std::fabs(v) * 0.01f), 0.0f, 0.0f, "%.3f")) {
				r_value = Variant((double)v);
				return true;
			}
			return false;
		}
		case Variant::STRING: {
			std::string v = r_value.as_string();
			if (p_info.hint == PROPERTY_HINT_MULTILINE_TEXT) {
				if (ImGui::InputTextMultiline("##v", &v, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f))) {
					r_value = Variant(v);
					return true;
				}
				return false;
			}
			if (p_info.hint == PROPERTY_HINT_ENUM) {
				bool changed = false;
				if (ImGui::BeginCombo("##v", v.c_str())) {
					for (const String &option : str_split(p_info.hint_string, ",")) {
						const String o = str_strip(option);
						if (ImGui::Selectable(o.c_str(), o == v)) {
							r_value = Variant(o);
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				return changed;
			}
			if (p_info.hint == PROPERTY_HINT_FILE) {
				bool changed = false;
				ImGui::SetNextItemWidth(-36);
				if (ImGui::InputText("##v", &v)) {
					r_value = Variant(v);
					changed = true;
				}
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MEX_FILE")) {
						r_value = Variant(String((const char *)payload->Data));
						changed = true;
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::SameLine();
				if (ImGui::Button("...", ImVec2(-FLT_MIN, 0))) {
					std::vector<String> extensions;
					for (const String &e : str_split(p_info.hint_string, ",", false)) {
						String ext = str_strip(e);
						if (str_begins_with(ext, "*.")) {
							ext = ext.substr(2);
						}
						extensions.push_back(ext);
					}
					const ObjectID id = p_object->get_instance_id();
					const String property = p_info.name;
					_open_file_dialog("Choisir un fichier", extensions, false, [this, id, property](const String &p_path) {
						if (Object *obj = ObjectDB::get_instance(id)) {
							obj->set(property, Variant(p_path));
							_mark_modified();
						}
					});
				}
				return changed;
			}
			if (ImGui::InputText("##v", &v)) {
				r_value = Variant(v);
				return true;
			}
			return false;
		}
		case Variant::VECTOR2: {
			Vector2 v = r_value.as_vector2();
			float f[2] = { v.x, v.y };
			if (ImGui::DragFloat2("##v", f, 0.5f, 0.0f, 0.0f, "%.2f")) {
				r_value = Variant(Vector2(f[0], f[1]));
				return true;
			}
			return false;
		}
		case Variant::VECTOR3: {
			Vector3 v = r_value.as_vector3();
			const bool radians = p_info.hint == PROPERTY_HINT_RADIANS;
			float f[3] = { v.x, v.y, v.z };
			if (radians) {
				for (float &c : f) {
					c = (float)Math::rad_to_deg(c);
				}
			}
			if (ImGui::DragFloat3("##v", f, radians ? 1.0f : 0.05f, 0.0f, 0.0f, radians ? "%.1f°" : "%.3f")) {
				if (radians) {
					for (float &c : f) {
						c = (float)Math::deg_to_rad(c);
					}
				}
				r_value = Variant(Vector3(f[0], f[1], f[2]));
				return true;
			}
			return false;
		}
		case Variant::RECT2: {
			Rect2 r = r_value.as_rect2();
			float f[4] = { r.position.x, r.position.y, r.size.x, r.size.y };
			if (ImGui::DragFloat4("##v", f, 0.5f, 0.0f, 0.0f, "%.1f")) {
				r_value = Variant(Rect2(f[0], f[1], f[2], f[3]));
				return true;
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
				ImGui::SetTooltip("x, y, largeur, hauteur");
			}
			return false;
		}
		case Variant::COLOR: {
			Color c = r_value.as_color();
			float f[4] = { c.r, c.g, c.b, c.a };
			if (ImGui::ColorEdit4("##v", f, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
				r_value = Variant(Color(f[0], f[1], f[2], f[3]));
				return true;
			}
			return false;
		}
		case Variant::OBJECT:
			return _edit_resource(p_object, p_info, r_value);
		default: {
			// Tableaux, dictionnaires... : édition sous forme de texte (valider avec Entrée).
			const String key = itos((int64_t)p_object->get_instance_id()) + "/" + p_info.name;
			std::string &buffer = text_edit_buffers[key];
			if (ImGui::GetActiveID() != ImGui::GetID("##v")) {
				buffer = VariantWriter::write(r_value);
			}
			if (ImGui::InputText("##v", &buffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
				VariantParser::Stream stream(buffer);
				Variant parsed;
				String error;
				if (VariantParser::parse_value(stream, parsed, error) == OK) {
					r_value = parsed;
					return true;
				}
				print_error("Valeur invalide pour '" + p_info.name + "' : " + error);
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
				ImGui::SetTooltip("Exemples : [1, 2, 3]   {\"cle\": 1}   Vector2(1, 2)\nValider avec Entrée.");
			}
			return false;
		}
	}
}

bool EditorNode::_edit_resource(Object *p_object, const PropertyInfo &p_info, Variant &r_value) {
	Ref<Resource> resource = Ref<Resource>(Object::cast_to<Resource>(r_value.as_object()));
	const String base_type = (p_info.hint == PROPERTY_HINT_RESOURCE_TYPE && !p_info.hint_string.empty()) ? p_info.hint_string : String("Resource");
	const bool is_script = ClassDB::is_parent_class(base_type, "Script");
	const ObjectID object_id = p_object->get_instance_id();
	const String property = p_info.name;
	bool changed = false;

	String label = "<vide>";
	if (resource.is_valid()) {
		label = resource->get_path().empty() ? "[" + resource->get_class() + "]" : path_get_file(resource->get_path());
	} else if (!r_value.is_null()) {
		label = "<objet>";
	}
	if (ImGui::Button((label + "##res").c_str(), ImVec2(-FLT_MIN, 0))) {
		ImGui::OpenPopup("##resmenu");
	}
	if (resource.is_valid() && !resource->get_path().empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
		ImGui::SetTooltip("%s", resource->get_path().c_str());
	}
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MEX_FILE")) {
			const String path((const char *)payload->Data);
			const String type = ResourceLoader::get_resource_type(path);
			if (!type.empty() && ClassDB::is_parent_class(type, base_type)) {
				Ref<Resource> loaded = ResourceLoader::load(path);
				if (loaded.is_valid()) {
					r_value = Variant(loaded);
					changed = true;
				}
			} else {
				print_warning("Ce fichier n'est pas du type attendu (" + base_type + ") : " + path);
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopup("##resmenu")) {
		if (is_script) {
			if (ImGui::MenuItem("Nouveau script...")) {
				Node *node = Object::cast_to<Node>(p_object);
				const String suggestion = "res://" + (node ? str_to_lower(node->get_name()) : String("script")) + ".mx";
				_open_file_dialog("Attacher un script (nouveau ou existant)", { "mx" }, true, [this, object_id](const String &p_path) {
					_attach_script(Object::cast_to<Node>(ObjectDB::get_instance(object_id)), p_path);
				}, suggestion);
			}
			if (resource.is_valid() && ImGui::MenuItem("Modifier le script")) {
				const String path = resource->get_path();
				_defer([this, path]() { _open_script(path); });
			}
		} else {
			// Types créables directement (formes...) : ceux qui ne viennent pas d'un fichier.
			std::vector<String> classes;
			ClassDB::get_class_list(&classes);
			std::sort(classes.begin(), classes.end());
			for (const String &cls : classes) {
				if (!ClassDB::is_parent_class(cls, base_type) || !ClassDB::is_parent_class(cls, "Resource") || !ClassDB::can_instantiate(cls)) {
					continue;
				}
				if (cls == "Resource" || ClassDB::is_parent_class(cls, "Script")) {
					continue;
				}
				std::vector<String> extensions;
				ResourceLoader::get_extensions_for_type(cls, &extensions);
				if (!extensions.empty()) {
					continue;
				}
				if (ImGui::MenuItem(("Nouveau " + cls).c_str())) {
					Ref<Resource> created = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(cls)));
					if (created.is_valid()) {
						r_value = Variant(created);
						changed = true;
					}
				}
			}
		}
		if (ImGui::MenuItem("Charger un fichier...")) {
			std::vector<String> extensions;
			ResourceLoader::get_extensions_for_type(base_type, &extensions);
			if (!is_script && std::find(extensions.begin(), extensions.end(), "res") == extensions.end()) {
				extensions.push_back("res");
			}
			_open_file_dialog("Charger : " + base_type, extensions, false, [this, object_id, property, base_type](const String &p_path) {
				Object *obj = ObjectDB::get_instance(object_id);
				if (!obj) {
					return;
				}
				Ref<Resource> loaded = ResourceLoader::load(p_path);
				if (loaded.is_null()) {
					return;
				}
				if (!ClassDB::is_parent_class(loaded->get_class(), base_type)) {
					print_error("Type de ressource inattendu (" + loaded->get_class() + ", attendu : " + base_type + ").");
					return;
				}
				ScriptingDisabledScope no_scripts;
				obj->set(property, Variant(loaded));
				_mark_modified();
			});
		}
		if (resource.is_valid() && !is_script) {
			if (ImGui::MenuItem("Rendre unique (copie)")) {
				Ref<Resource> copy = resource->duplicate();
				if (copy.is_valid()) {
					r_value = Variant(copy);
					changed = true;
				}
			}
			if (resource->get_path().empty() && ImGui::MenuItem("Enregistrer dans un fichier...")) {
				Ref<Resource> to_save = resource;
				_open_file_dialog("Enregistrer la ressource", { "res" }, true, [this, to_save](const String &p_path) {
					if (ResourceSaver::save(p_path, to_save) == OK) {
						fs_dirty = true;
						_mark_modified();
					}
				});
			}
		}
		if (!r_value.is_null() && ImGui::MenuItem("Vider")) {
			r_value = Variant();
			changed = true;
		}
		ImGui::EndPopup();
	}

	// Ressource intégrée à la scène : ses propriétés sont éditables sur place.
	if (resource.is_valid() && !is_script && !changed) {
		const bool builtin = resource->get_path().empty();
		ImGui::Indent(10);
		if (ImGui::TreeNodeEx("##sub", ImGuiTreeNodeFlags_SpanAvailWidth | (builtin ? ImGuiTreeNodeFlags_DefaultOpen : 0), "%s%s", resource->get_class().c_str(), builtin ? "" : " (fichier partagé)")) {
			_draw_object_properties(resource.ptr(), false);
			ImGui::TreePop();
		}
		ImGui::Unindent(10);
	}
	return changed;
}

// ---------------------------------------------------------------------------
// Signaux
// ---------------------------------------------------------------------------
void EditorNode::_draw_node_signals(Node *p_node) {
	std::vector<MethodInfo> signals;
	Ref<Script> script = Ref<Script>(Object::cast_to<Script>(p_node->get_script().as_object()));
	if (script.is_valid()) {
		script->get_script_signal_list(&signals);
	}
	ClassDB::get_signal_list(p_node->get_class(), &signals);
	if (signals.empty()) {
		ImGui::TextDisabled("Ce nœud n'a aucun signal.");
		return;
	}
	const ObjectID id = p_node->get_instance_id();
	for (const MethodInfo &signal : signals) {
		std::vector<String> arg_names;
		for (const PropertyInfo &arg : signal.arguments) {
			arg_names.push_back(arg.name);
		}
		const String arguments = str_join(arg_names, ", ");
		std::vector<Object::Connection> connections;
		p_node->get_signal_connection_list(signal.name, &connections);

		ImGui::PushID(signal.name.c_str());
		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.50f, 1.0f));
		ImGui::Text("%s(%s)", signal.name.c_str(), arguments.c_str());
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (ImGui::SmallButton("Connecter...")) {
			connect_dialog.open_request = true;
			connect_dialog.source_id = id;
			connect_dialog.signal = signal.name;
			connect_dialog.arguments = arguments;
			connect_dialog.target_path = ".";
			connect_dialog.method = "_on_" + _snake_case(p_node->get_name()) + "_" + signal.name;
		}
		int index = 0;
		for (const Object::Connection &c : connections) {
			if (!(c.flags & Object::CONNECT_PERSIST)) {
				continue;
			}
			Node *target = Object::cast_to<Node>(c.callable.get_object());
			if (!target || !scene_root) {
				continue;
			}
			ImGui::PushID(index++);
			const String path = target == scene_root ? String(".") : scene_root->get_path_to(target);
			ImGui::Indent(14);
			if (ImGui::SmallButton("x")) {
				const Callable callable = c.callable;
				const String signal_name = signal.name;
				_defer([this, id, signal_name, callable]() {
					if (Object *obj = ObjectDB::get_instance(id)) {
						obj->disconnect(signal_name, callable);
						_mark_modified();
					}
				});
			}
			ImGui::SetItemTooltip("Déconnecter");
			ImGui::SameLine();
			ImGui::Text("-> %s :: %s()", path.c_str(), c.callable.method.c_str());
			ImGui::Unindent(14);
			ImGui::PopID();
		}
		ImGui::PopID();
	}
}

static void _collect_node_paths(Node *p_root, Node *p_node, std::vector<String> &r_paths) {
	r_paths.push_back(p_node == p_root ? String(".") : p_root->get_path_to(p_node));
	for (Node *child : p_node->get_children_vector()) {
		if (child->get_owner() == p_root) {
			_collect_node_paths(p_root, child, r_paths);
		}
	}
}

void EditorNode::_draw_connect_dialog() {
	if (connect_dialog.open_request) {
		connect_dialog.open_request = false;
		ImGui::OpenPopup("Connecter un signal###Connect");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Connecter un signal###Connect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}
	Node *source = Object::cast_to<Node>(ObjectDB::get_instance(connect_dialog.source_id));
	if (!source || !scene_root) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}
	ImGui::Text("Signal : %s(%s)", connect_dialog.signal.c_str(), connect_dialog.arguments.c_str());
	ImGui::Text("Émis par : %s", source->get_name().c_str());
	ImGui::Spacing();

	std::vector<String> paths;
	_collect_node_paths(scene_root, scene_root, paths);
	ImGui::SetNextItemWidth(360);
	if (ImGui::BeginCombo("Nœud cible", connect_dialog.target_path == "." ? (scene_root->get_name() + " (racine)").c_str() : connect_dialog.target_path.c_str())) {
		for (const String &path : paths) {
			const String text = path == "." ? scene_root->get_name() + " (racine)" : path;
			if (ImGui::Selectable(text.c_str(), path == connect_dialog.target_path)) {
				connect_dialog.target_path = path;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetNextItemWidth(360);
	ImGui::InputText("Méthode", &connect_dialog.method);

	Node *target = connect_dialog.target_path == "." ? scene_root : scene_root->get_node_or_null(connect_dialog.target_path);
	Ref<Script> target_script;
	if (target) {
		target_script = Ref<Script>(Object::cast_to<Script>(target->get_script().as_object()));
	}
	const String method = str_strip(connect_dialog.method);
	const bool method_valid = str_is_valid_identifier(method);
	const bool needs_stub = target && target_script.is_valid() && method_valid && !target_script->has_method(method) && !ClassDB::get_method(ClassDB::get_class_info(target->get_class()), method);
	if (target && target_script.is_null() && method_valid && !ClassDB::get_method(ClassDB::get_class_info(target->get_class()), method)) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.83f, 0.40f, 1.0f));
		ImGui::TextWrapped("Le nœud cible n'a pas de script : attachez-en un contenant cette méthode.");
		ImGui::PopStyleColor();
	} else if (needs_stub) {
		ImGui::TextDisabled("La fonction sera ajoutée à la fin du script cible.");
	}
	ImGui::Spacing();

	ImGui::BeginDisabled(!target || !method_valid);
	if (ImGui::Button("Connecter", ImVec2(150, 0))) {
		const ObjectID source_id = connect_dialog.source_id;
		const ObjectID target_id = target->get_instance_id();
		const String signal = connect_dialog.signal;
		const String arguments = connect_dialog.arguments;
		_defer([this, source_id, target_id, signal, method, arguments, needs_stub]() {
			Object *src = ObjectDB::get_instance(source_id);
			Node *dst = Object::cast_to<Node>(ObjectDB::get_instance(target_id));
			if (!src || !dst) {
				return;
			}
			const Callable callable(dst, method);
			if (!src->is_connected(signal, callable)) {
				src->connect(signal, callable, Object::CONNECT_PERSIST);
				_mark_modified();
			}
			if (needs_stub) {
				Resource *script = Object::cast_to<Resource>(dst->get_script().as_object());
				if (script && !script->get_path().empty()) {
					const String path = script->get_path();
					if (script_path == path && script_dirty) {
						_save_script();
					}
					String code = FileAccess::get_file_as_string(path);
					while (!code.empty() && (code.back() == '\n' || code.back() == ' ' || code.back() == '\t' || code.back() == '\r')) {
						code.pop_back();
					}
					code += "\n\nfunc " + method + "(" + arguments + "):\n\tpass\n";
					if (FileAccess::write_string(path, code) == OK) {
						_reload_script_resource(path, code);
						script_path.clear(); // force le rechargement dans l'éditeur de script
						_open_script(path);
					}
				}
			}
		});
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Annuler", ImVec2(120, 0))) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Groupes
// ---------------------------------------------------------------------------
void EditorNode::_draw_node_groups(Node *p_node) {
	const ObjectID id = p_node->get_instance_id();
	ImGui::TextDisabled("Les groupes servent à retrouver des nœuds :\nget_tree().get_nodes_in_group(\"ennemis\")");
	ImGui::Spacing();
	ImGui::SetNextItemWidth(-90);
	const bool entered = ImGui::InputTextWithHint("##group", "Nom du groupe", &group_buffer, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	if (ImGui::Button("Ajouter", ImVec2(-FLT_MIN, 0)) || entered) {
		const String group = str_strip(group_buffer);
		if (!group.empty() && !p_node->is_in_group(group)) {
			p_node->add_to_group(group);
			_mark_modified();
		}
		group_buffer.clear();
	}
	ImGui::Separator();
	const std::set<String> groups = p_node->get_groups_set();
	if (groups.empty()) {
		ImGui::TextDisabled("Ce nœud n'appartient à aucun groupe.");
	}
	for (const String &group : groups) {
		ImGui::PushID(group.c_str());
		if (ImGui::SmallButton("x")) {
			_defer([this, id, group]() {
				if (Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id))) {
					node->remove_from_group(group);
					_mark_modified();
				}
			});
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(group.c_str());
		ImGui::PopID();
	}
}
