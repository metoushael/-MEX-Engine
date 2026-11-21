// Dock « Animation » : édition des animations d'un AnimatedSprite2D (plages d'images) et
// d'un AnimationPlayer (pistes de propriétés et images clés), avec aperçu dans la vue 2D.
#include "editor_node.h"

#include "core/string/ustring.h"
#include "scene/2d/animated_sprite_2d.h"
#include "scene/animation/animation_player.h"
#include "scene/main/node.h"

#include "editor_ui.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cmath>

namespace {

void _collect_paths(Node *p_base, Node *p_node, std::vector<String> &r_paths) {
	r_paths.push_back(p_node == p_base ? String(".") : p_base->get_path_to(p_node));
	for (Node *child : p_node->get_children_vector()) {
		_collect_paths(p_base, child, r_paths);
	}
}

// Propriétés qu'une piste peut animer.
bool _is_animatable(const PropertyInfo &p_info) {
	if (p_info.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP)) {
		return false;
	}
	switch (p_info.type) {
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::VECTOR2:
		case Variant::VECTOR3:
		case Variant::COLOR:
		case Variant::STRING:
			return p_info.name != "name" && p_info.name != "anchors_preset";
		default:
			return false;
	}
}

String _unique_name(const Dictionary &p_dict, const String &p_base) {
	if (!p_dict.has(Variant(p_base))) {
		return p_base;
	}
	for (int i = 2;; i++) {
		const String name = p_base + itos(i);
		if (!p_dict.has(Variant(name))) {
			return name;
		}
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Aperçu : les valeurs d'origine des propriétés animées sont mises de côté, puis
// restaurées à la fin de l'aperçu. Elles ne sont jamais enregistrées dans la scène.
// ---------------------------------------------------------------------------
void EditorNode::_anim_backup_property(Object *p_object, const String &p_property) {
	const ObjectID id = p_object->get_instance_id();
	for (const AnimBackup &backup : anim_backup) {
		if (backup.id == id && backup.property == p_property) {
			return;
		}
	}
	bool valid = false;
	const Variant value = p_object->get(p_property, &valid);
	if (valid) {
		anim_backup.push_back({ id, p_property, value });
	}
}

void EditorNode::_anim_swap_backup() const {
	for (AnimBackup &backup : anim_backup) {
		Object *object = ObjectDB::get_instance(backup.id);
		if (!object) {
			continue;
		}
		const Variant current = object->get(backup.property);
		object->set(backup.property, backup.value);
		backup.value = current;
	}
}

void EditorNode::_anim_end_preview() {
	anim_playing = false;
	if (AnimatedSprite2D *sprite = Object::cast_to<AnimatedSprite2D>(ObjectDB::get_instance(anim_target_id))) {
		sprite->pause();
	}
	// Restaure dans l'ordre inverse (la première valeur mise de côté est la bonne).
	for (auto it = anim_backup.rbegin(); it != anim_backup.rend(); ++it) {
		if (Object *object = ObjectDB::get_instance(it->id)) {
			object->set(it->property, it->value);
		}
	}
	anim_backup.clear();
}

void EditorNode::_anim_preview_apply(AnimationPlayer *p_player) {
	Node *base = p_player->get_parent();
	if (!base || !p_player->has_animation(anim_current)) {
		return;
	}
	const Array track_list = p_player->get_track_list(anim_current);
	for (const Variant &track_name : track_list.vec()) {
		const String track = track_name.as_string();
		const size_t colon = track.rfind(':');
		if (colon == String::npos) {
			continue;
		}
		const String path = track.substr(0, colon);
		Node *node = (path.empty() || path == ".") ? base : base->get_node_or_null(path);
		if (node) {
			_anim_backup_property(node, track.substr(colon + 1));
		}
	}
	p_player->apply(anim_current, anim_time);
}

// ---------------------------------------------------------------------------
// Dock
// ---------------------------------------------------------------------------
void EditorNode::_draw_animation_dock() {
	// La cible suit la sélection, mais reste affichée quand on sélectionne un autre nœud
	// (pour pouvoir le déplacer puis poser une clé).
	Node *selected = _get_selected();
	if (selected && selected->get_instance_id() != anim_target_id &&
			(Object::cast_to<AnimationPlayer>(selected) || Object::cast_to<AnimatedSprite2D>(selected))) {
		_anim_end_preview();
		anim_target_id = selected->get_instance_id();
		anim_target_path = scene_root ? scene_root->get_path_to(selected) : String();
		if (selected == scene_root) {
			anim_target_path = ".";
		}
		anim_current.clear();
		anim_time = 0;
		anim_selected_key = -1;
		focus_animation = true;
	}
	Node *target = Object::cast_to<Node>(ObjectDB::get_instance(anim_target_id));
	if (!target && scene_root && !anim_target_path.empty()) {
		// Après une annulation, la scène est reconstruite : on retrouve la cible par son chemin.
		Node *found = anim_target_path == "." ? scene_root : scene_root->get_node_or_null(anim_target_path);
		if (found && (Object::cast_to<AnimationPlayer>(found) || Object::cast_to<AnimatedSprite2D>(found))) {
			target = found;
			anim_target_id = found->get_instance_id();
		} else {
			anim_target_path.clear();
			anim_target_id = 0;
		}
	}

	if (focus_animation) {
		ImGui::SetNextWindowFocus();
		focus_animation = false;
	}
	if (!ImGui::Begin(ICON_DOCK_ANIMATION "  Animation###Animation")) {
		ImGui::End();
		return;
	}
	if (!target) {
		ImGui::TextDisabled("Sélectionnez un AnimationPlayer ou un AnimatedSprite2D pour modifier ses animations.");
		ImGui::End();
		return;
	}
	const bool editable = _is_editable(target);
	ImGui::BeginDisabled(!editable);
	if (AnimatedSprite2D *sprite = Object::cast_to<AnimatedSprite2D>(target)) {
		_draw_animated_sprite_editor(sprite);
	} else if (AnimationPlayer *player = Object::cast_to<AnimationPlayer>(target)) {
		_draw_animation_player_editor(player);
	}
	ImGui::EndDisabled();
	ImGui::End();
}

// ---------------------------------------------------------------------------
// AnimatedSprite2D
// ---------------------------------------------------------------------------
void EditorNode::_draw_animated_sprite_editor(AnimatedSprite2D *p_sprite) {
	const int frame_count = std::max(1, p_sprite->get_hframes() * p_sprite->get_vframes());
	Dictionary animations = p_sprite->get_animations();

	ImGui::AlignTextToFramePadding();
	ImGui::Text("%s  -  planche de %d x %d = %d image(s)", p_sprite->get_name().c_str(), p_sprite->get_hframes(), p_sprite->get_vframes(), frame_count);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
		ImGui::SetTooltip("Le découpage de la texture se règle dans l'inspecteur (Hframes, Vframes).\nLes images sont numérotées de gauche à droite puis de haut en bas, à partir de 0.");
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Animation")) {
		const String name = _unique_name(animations, "animation");
		p_sprite->add_animation(name, 0, frame_count - 1, 8.0f, true);
		_mark_modified();
	}
	if (!anim_backup.empty()) {
		ImGui::SameLine();
		if (ImGui::Button("Arrêter l'aperçu")) {
			_anim_end_preview();
		}
	}
	if (frame_count <= 1) {
		ImGui::TextColored(EditorUI::col(palette.warning), "La texture n'est pas découpée : réglez Hframes / Vframes dans l'inspecteur.");
	}

	// Aperçu : avance l'animation comme en jeu.
	if (p_sprite->is_playing()) {
		p_sprite->advance(frame_delta);
	}

	String to_remove;
	String rename_from, rename_to;
	if (ImGui::BeginTable("##anims", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
		ImGui::TableSetupColumn("Nom", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Première", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("Dernière", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("Images/s", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("Boucle", ImGuiTableColumnFlags_WidthFixed, 50);
		ImGui::TableSetupColumn("Au démarrage", ImGuiTableColumnFlags_WidthFixed, 95);
		ImGui::TableSetupColumn("Aperçu", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();

		const Array names = animations.keys();
		for (const Variant &key : names.vec()) {
			const String name = key.as_string();
			Array data = animations[key].as_array();
			while (data.size() < 4) {
				data.push_back(data.size() == 2 ? Variant(8.0) : data.size() == 3 ? Variant(true) : Variant(0));
			}
			ImGui::PushID(name.c_str());
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			std::string &buffer = text_edit_buffers["anim_name:" + name];
			if (buffer.empty() && !ImGui::IsAnyItemActive()) {
				buffer = name;
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##name", &buffer);
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				const String new_name = str_strip(buffer);
				if (!new_name.empty() && new_name != name && !animations.has(Variant(new_name))) {
					rename_from = name;
					rename_to = new_name;
				} else {
					buffer = name;
				}
			}

			bool changed = false;
			ImGui::TableNextColumn();
			int first = (int)data[0].as_int();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragInt("##first", &first, 0.2f, 0, frame_count - 1)) {
				data[0] = Variant(Math::clamp(first, 0, frame_count - 1));
				changed = true;
			}
			ImGui::TableNextColumn();
			int last = (int)data[1].as_int();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragInt("##last", &last, 0.2f, 0, frame_count - 1)) {
				data[1] = Variant(Math::clamp(last, 0, frame_count - 1));
				changed = true;
			}
			ImGui::TableNextColumn();
			float fps = (float)data[2].as_float();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat("##fps", &fps, 0.1f, 0.1f, 120.0f, "%.1f")) {
				data[2] = Variant((double)Math::clamp(fps, 0.1f, 120.0f));
				changed = true;
			}
			ImGui::TableNextColumn();
			bool loop = data[3].as_bool();
			if (ImGui::Checkbox("##loop", &loop)) {
				data[3] = Variant(loop);
				changed = true;
			}
			ImGui::TableNextColumn();
			bool autoplay = p_sprite->get_autoplay() == name;
			if (ImGui::Checkbox("##auto", &autoplay)) {
				p_sprite->set_autoplay(autoplay ? name : String());
				changed = true;
			}
			if (changed) {
				_mark_modified();
			}

			ImGui::TableNextColumn();
			const bool previewing = p_sprite->is_playing() && p_sprite->get_animation() == name;
			if (ImGui::Button(previewing ? "Arrêter" : "Lire", ImVec2(-FLT_MIN, 0))) {
				if (previewing) {
					_anim_end_preview();
				} else {
					_anim_backup_property(p_sprite, "frame");
					_anim_backup_property(p_sprite, "animation");
					p_sprite->stop();
					p_sprite->set_animation(String());
					p_sprite->play(name);
				}
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Supprimer", ImVec2(-FLT_MIN, 0))) {
				to_remove = name;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (!to_remove.empty()) {
		_anim_end_preview();
		p_sprite->remove_animation(to_remove);
		if (p_sprite->get_autoplay() == to_remove) {
			p_sprite->set_autoplay(String());
		}
		text_edit_buffers.erase("anim_name:" + to_remove);
		_mark_modified();
	}
	if (!rename_from.empty()) {
		_anim_end_preview();
		const Variant data = animations[Variant(rename_from)];
		animations.erase(Variant(rename_from));
		animations[Variant(rename_to)] = data;
		if (p_sprite->get_autoplay() == rename_from) {
			p_sprite->set_autoplay(rename_to);
		}
		if (p_sprite->get_animation() == rename_from) {
			p_sprite->set_animation(rename_to);
		}
		text_edit_buffers.erase("anim_name:" + rename_from);
		_mark_modified();
	}
}

// ---------------------------------------------------------------------------
// AnimationPlayer
// ---------------------------------------------------------------------------
void EditorNode::_draw_animation_player_editor(AnimationPlayer *p_player) {
	Node *base = p_player->get_parent();
	Dictionary animations = p_player->get_animations();
	if (!p_player->has_animation(anim_current)) {
		anim_current = animations.is_empty() ? String() : animations.keys()[0].as_string();
		anim_time = 0;
		anim_selected_key = -1;
	}

	// --- Barre : choix de l'animation -------------------------------------------------
	ImGui::SetNextItemWidth(ES(170));
	if (ImGui::BeginCombo("##anim", anim_current.empty() ? "(aucune animation)" : anim_current.c_str())) {
		const Array names = animations.keys();
		for (const Variant &key : names.vec()) {
			const String name = key.as_string();
			if (ImGui::Selectable(name.c_str(), name == anim_current) && name != anim_current) {
				_anim_end_preview();
				anim_current = name;
				anim_time = 0;
				anim_selected_key = -1;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Nouvelle...")) {
		anim_new_name = _unique_name(animations, "animation");
		ImGui::OpenPopup("##new_anim");
	}
	if (ImGui::BeginPopup("##new_anim")) {
		ImGui::TextUnformatted("Nom de l'animation :");
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		const bool entered = ImGui::InputText("##name", &anim_new_name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		const String name = str_strip(anim_new_name);
		const bool valid = !name.empty() && !animations.has(Variant(name));
		ImGui::BeginDisabled(!valid);
		if ((ImGui::Button("Créer") || entered) && valid) {
			_anim_end_preview();
			p_player->add_animation(name, 1.0f, false);
			anim_current = name;
			anim_time = 0;
			anim_selected_key = -1;
			_mark_modified();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}
	if (anim_current.empty()) {
		ImGui::TextDisabled("Créez une animation, puis ajoutez des pistes : chaque piste anime une propriété d'un nœud.");
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Supprimer")) {
		_anim_end_preview();
		p_player->remove_animation(anim_current);
		if (p_player->get_autoplay() == anim_current) {
			p_player->set_autoplay(String());
		}
		anim_current.clear();
		_mark_modified();
		return;
	}
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Durée (s)");
	ImGui::SameLine();
	float length = p_player->get_animation_length(anim_current);
	ImGui::SetNextItemWidth(ES(70));
	if (ImGui::DragFloat("##length", &length, 0.01f, 0.05f, 600.0f, "%.2f")) {
		p_player->set_animation_length(anim_current, Math::clamp(length, 0.05f, 600.0f));
		_mark_modified();
	}
	length = p_player->get_animation_length(anim_current);
	ImGui::SameLine();
	bool loop = p_player->get_animation_loop(anim_current);
	if (ImGui::Checkbox("Boucle", &loop)) {
		p_player->set_animation_loop(anim_current, loop);
		_mark_modified();
	}
	ImGui::SameLine();
	bool autoplay = p_player->get_autoplay() == anim_current;
	if (ImGui::Checkbox("Au démarrage", &autoplay)) {
		p_player->set_autoplay(autoplay ? anim_current : String());
		_mark_modified();
	}

	// --- Barre : lecture ---------------------------------------------------------------
	bool apply = false;
	if (ImGui::Button(anim_playing ? "Pause" : "Lire", ImVec2(ES(60), 0))) {
		anim_playing = !anim_playing;
		if (anim_playing && anim_time >= length) {
			anim_time = 0;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Début")) {
		anim_playing = false;
		anim_time = 0;
		apply = true;
	}
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Temps");
	ImGui::SameLine();
	float time = (float)anim_time;
	ImGui::SetNextItemWidth(ES(70));
	if (ImGui::DragFloat("##time", &time, 0.01f, 0.0f, length, "%.2f")) {
		anim_time = Math::clamp(time, 0.0f, length);
		anim_playing = false;
		apply = true;
	}
	if (!anim_backup.empty()) {
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Button, EditorUI::col(palette.accent, 0.55f));
		if (ImGui::Button("Terminer l'aperçu")) {
			_anim_end_preview();
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
			ImGui::SetTooltip("Pendant l'aperçu, les propriétés animées prennent la valeur de l'animation.\nElles retrouvent leur valeur d'origine à la fin de l'aperçu (et ne sont jamais enregistrées ainsi).");
		}
	}
	if (anim_playing) {
		anim_time += frame_delta * p_player->get_speed_scale();
		if (anim_time >= length) {
			if (loop) {
				anim_time = std::fmod(anim_time, (double)length);
			} else {
				anim_time = length;
				anim_playing = false;
			}
		}
		apply = true;
	}
	anim_time = Math::clamp(anim_time, 0.0, (double)length);

	// --- Ajout d'une piste -----------------------------------------------------------------
	if (base) {
		std::vector<String> paths;
		_collect_paths(base, base, paths);
		// Par défaut, la piste vise le nœud sélectionné dans l'arbre de scène.
		Node *selected = _get_selected();
		if (selected && selected->get_instance_id() != anim_last_selected) {
			anim_last_selected = selected->get_instance_id();
			if (selected != p_player && (selected == base || base->is_ancestor_of(selected))) {
				anim_track_node = selected == base ? String(".") : base->get_path_to(selected);
				anim_track_property.clear();
			}
		}
		if (std::find(paths.begin(), paths.end(), anim_track_node) == paths.end()) {
			anim_track_node = ".";
		}
		ImGui::SameLine(0, 12);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Nouvelle piste :");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ES(130));
		if (ImGui::BeginCombo("##track_node", anim_track_node == "." ? (base->get_name() + " (.)").c_str() : anim_track_node.c_str())) {
			for (const String &path : paths) {
				if (ImGui::Selectable(path == "." ? (base->get_name() + " (.)").c_str() : path.c_str(), path == anim_track_node)) {
					anim_track_node = path;
					anim_track_property.clear();
				}
			}
			ImGui::EndCombo();
		}
		Node *track_node = anim_track_node == "." ? base : base->get_node_or_null(anim_track_node);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ES(120));
		if (ImGui::BeginCombo("##track_property", anim_track_property.empty() ? "propriété..." : anim_track_property.c_str())) {
			if (track_node) {
				std::vector<PropertyInfo> list;
				track_node->get_property_list(&list);
				for (const PropertyInfo &pi : list) {
					if (_is_animatable(pi) && ImGui::Selectable(pi.name.c_str(), pi.name == anim_track_property)) {
						anim_track_property = pi.name;
					}
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!track_node || anim_track_property.empty());
		if (ImGui::Button("Ajouter")) {
			const String track = anim_track_node + ":" + anim_track_property;
			p_player->track_insert_key(anim_current, track, (real_t)anim_time, track_node->get(anim_track_property));
			anim_selected_track = track;
			anim_selected_key = -1;
			_mark_modified();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("Crée la piste et pose une première clé à l'instant courant,\navec la valeur actuelle de la propriété.");
		}
	}

	// --- Pistes et images clés -------------------------------------------------------------------
	const float name_width = 250.0f;
	const float row_height = ImGui::GetFrameHeight();
	String remove_track;
	const float editor_height = row_height + 10.0f; // ligne de la clé sélectionnée
	ImGui::BeginChild("##tracks", ImVec2(0, -editor_height), ImGuiChildFlags_Borders);
	ImDrawList *draw_list = ImGui::GetWindowDrawList();
	const float timeline_width = std::max(60.0f, ImGui::GetContentRegionAvail().x - name_width - 12.0f);
	auto time_to_x = [&](float p_origin, double p_time) {
		return p_origin + 6.0f + (float)(p_time / length) * (timeline_width - 12.0f);
	};
	auto x_to_time = [&](float p_origin, float p_x) {
		// Le curseur s'aimante au centième de seconde.
		const double t = (double)((p_x - p_origin - 6.0f) / (timeline_width - 12.0f)) * length;
		return Math::clamp(std::round(t * 100.0) / 100.0, 0.0, (double)length);
	};

	// Règle graduée : cliquer ou glisser déplace le curseur de temps.
	{
		ImGui::Dummy(ImVec2(1, row_height));
		ImGui::SameLine(name_width + ImGui::GetStyle().ItemSpacing.x);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##ruler", ImVec2(timeline_width, row_height));
		if (ImGui::IsItemActive()) {
			anim_time = x_to_time(origin.x, ImGui::GetIO().MousePos.x);
			anim_playing = false;
			apply = true;
		}
		draw_list->AddRectFilled(origin, ImVec2(origin.x + timeline_width, origin.y + row_height), EditorUI::u32(palette.bg_dark));
		// Pas des graduations : environ une tous les 60 pixels.
		const double steps[] = { 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 30, 60 };
		double step = 60;
		for (double s : steps) {
			if (s / length * (timeline_width - 12.0f) >= 55.0f) {
				step = s;
				break;
			}
		}
		for (double t = 0; t <= length + 0.0001; t += step) {
			const float x = time_to_x(origin.x, t);
			draw_list->AddLine(ImVec2(x, origin.y + row_height - 6), ImVec2(x, origin.y + row_height), EditorUI::u32(palette.text_dim));
			char label[32];
			snprintf(label, sizeof(label), step < 1 ? "%.2f" : "%.0f", t);
			draw_list->AddText(ImVec2(x + 3, origin.y + 1), EditorUI::u32(palette.text_dim), label);
		}
	}

	const Array tracks = p_player->get_track_list(anim_current);
	if (tracks.is_empty()) {
		ImGui::TextDisabled("Aucune piste. Choisissez un nœud et une propriété ci-dessus, puis « Ajouter ».");
	}
	for (const Variant &track_name : tracks.vec()) {
		const String track = track_name.as_string();
		const size_t colon = track.rfind(':');
		const String path = track.substr(0, colon);
		const String property = track.substr(colon + 1);
		Node *node = !base ? nullptr : (path.empty() || path == ".") ? base : base->get_node_or_null(path);
		ImGui::PushID(track.c_str());

		// Colonne de gauche : nom de la piste, bouton « clé », suppression.
		const ImVec2 row_start = ImGui::GetCursorScreenPos();
		ImGui::BeginDisabled(!node);
		if (ImGui::Button("+ Clé")) {
			_anim_backup_property(node, property); // la valeur posée reste visible pendant l'aperçu
			p_player->track_insert_key(anim_current, track, (real_t)anim_time, node->get(property));
			anim_selected_track = track;
			anim_selected_key = -1;
			_mark_modified();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip(node ? "Pose une clé à l'instant courant avec la valeur actuelle de la propriété." : "Nœud introuvable : la piste est ignorée.");
		}
		ImGui::SameLine();
		if (ImGui::Button("x")) {
			remove_track = track;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
			ImGui::SetTooltip("Supprimer la piste");
		}
		ImGui::SameLine();
		{
			const ImVec2 text_pos = ImGui::GetCursorScreenPos();
			ImGui::PushClipRect(text_pos, ImVec2(row_start.x + name_width - 4, text_pos.y + row_height), true);
			ImGui::AlignTextToFramePadding();
			if (node) {
				ImGui::Text("%s : %s", path == "." ? base->get_name().c_str() : path.c_str(), property.c_str());
			} else {
				ImGui::TextColored(EditorUI::col(palette.error), "%s : %s", path.c_str(), property.c_str());
			}
			ImGui::PopClipRect();
		}
		ImGui::SameLine(name_width + ImGui::GetStyle().ItemSpacing.x);

		// Bande de temps avec les clés.
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##strip", ImVec2(timeline_width, row_height));
		const bool strip_clicked = ImGui::IsItemClicked();
		const bool strip_active = ImGui::IsItemActive();
		const bool strip_hovered = ImGui::IsItemHovered();
		draw_list->AddRectFilled(origin, ImVec2(origin.x + timeline_width, origin.y + row_height - 2), EditorUI::u32(palette.frame));
		const int key_count = p_player->track_get_key_count(anim_current, track);
		const float mouse_x = ImGui::GetIO().MousePos.x;
		int hovered_key = -1;
		for (int i = 0; i < key_count; i++) {
			const float x = time_to_x(origin.x, p_player->track_get_key_time(anim_current, track, i));
			if (strip_hovered && std::fabs(mouse_x - x) <= 6.0f) {
				hovered_key = i;
			}
		}
		if (strip_clicked) {
			// Clic sur une clé : sélection (puis glisser pour la déplacer) ; ailleurs : curseur de temps.
			anim_drag_key = hovered_key >= 0;
			if (hovered_key >= 0) {
				anim_selected_track = track;
				anim_selected_key = hovered_key;
				anim_time = p_player->track_get_key_time(anim_current, track, hovered_key);
			} else {
				anim_time = x_to_time(origin.x, mouse_x);
			}
			anim_playing = false;
			apply = true;
		} else if (strip_active) {
			if (!anim_drag_key) {
				anim_time = x_to_time(origin.x, mouse_x);
				apply = true;
			} else if (anim_selected_track == track && anim_selected_key >= 0 && anim_selected_key < key_count && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
				const Variant value = p_player->track_get_key_value(anim_current, track, anim_selected_key);
				const real_t new_time = (real_t)(std::round(x_to_time(origin.x, mouse_x) * 100.0) / 100.0);
				// Une clé ne peut pas en écraser une autre en passant dessus.
				bool occupied = false;
				for (int i = 0; i < key_count; i++) {
					if (i != anim_selected_key && std::fabs(p_player->track_get_key_time(anim_current, track, i) - new_time) < 0.0005f) {
						occupied = true;
					}
				}
				if (!occupied && std::fabs(new_time - p_player->track_get_key_time(anim_current, track, anim_selected_key)) > 0.0001f) {
					p_player->track_remove_key(anim_current, track, anim_selected_key);
					p_player->track_insert_key(anim_current, track, new_time, value);
					anim_selected_key = -1;
					for (int i = 0; i < p_player->track_get_key_count(anim_current, track); i++) {
						if (std::fabs(p_player->track_get_key_time(anim_current, track, i) - new_time) < 0.0005f) {
							anim_selected_key = i;
						}
					}
					anim_time = new_time;
					apply = true;
					_mark_modified();
				}
			}
			anim_playing = false;
		}
		for (int i = 0; i < p_player->track_get_key_count(anim_current, track); i++) {
			const float x = time_to_x(origin.x, p_player->track_get_key_time(anim_current, track, i));
			const float y = origin.y + (row_height - 2) * 0.5f;
			const bool is_selected = anim_selected_track == track && anim_selected_key == i;
			const ImU32 color = is_selected ? EditorUI::u32(palette.accent) : (i == hovered_key ? EditorUI::u32(palette.text) : EditorUI::u32(palette.info));
			draw_list->AddQuadFilled(ImVec2(x, y - 6), ImVec2(x + 6, y), ImVec2(x, y + 6), ImVec2(x - 6, y), color);
		}
		// Curseur de temps.
		const float cursor_x = time_to_x(origin.x, anim_time);
		draw_list->AddLine(ImVec2(cursor_x, origin.y - 2), ImVec2(cursor_x, origin.y + row_height), IM_COL32(255, 90, 90, 255), 1.5f);
		ImGui::PopID();
	}
	ImGui::EndChild();

	if (anim_selected_key < 0) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Cliquez sur une clé pour la modifier, glissez-la pour la déplacer. Cliquez sur la règle pour déplacer le curseur de temps.");
	}
	if (!remove_track.empty()) {
		_anim_end_preview();
		p_player->remove_track(anim_current, remove_track);
		anim_selected_key = -1;
		_mark_modified();
	}

	// --- Clé sélectionnée ---------------------------------------------------------------------------
	if (anim_selected_key >= 0) {
		const int key_count = p_player->track_get_key_count(anim_current, anim_selected_track);
		if (anim_selected_key >= key_count) {
			anim_selected_key = -1;
		} else {
			const String track = anim_selected_track;
			const size_t colon = track.rfind(':');
			const String path = track.substr(0, colon);
			const String property = track.substr(colon + 1);
			Node *node = !base ? nullptr : (path.empty() || path == ".") ? base : base->get_node_or_null(path);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Clé %d de « %s »   Temps", anim_selected_key + 1, track.c_str());
			ImGui::SameLine();
			float key_time = p_player->track_get_key_time(anim_current, track, anim_selected_key);
			Variant value = p_player->track_get_key_value(anim_current, track, anim_selected_key);
			ImGui::SetNextItemWidth(ES(70));
			if (ImGui::DragFloat("##key_time", &key_time, 0.01f, 0.0f, length, "%.2f")) {
				key_time = Math::clamp(key_time, 0.0f, length);
				p_player->track_remove_key(anim_current, track, anim_selected_key);
				p_player->track_insert_key(anim_current, track, key_time, value);
				for (int i = 0; i < p_player->track_get_key_count(anim_current, track); i++) {
					if (std::fabs(p_player->track_get_key_time(anim_current, track, i) - key_time) < 0.0005f) {
						anim_selected_key = i;
					}
				}
				anim_time = key_time;
				apply = true;
				_mark_modified();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Valeur");
			ImGui::SameLine();
			PropertyInfo info(value.get_type(), property);
			if (node) {
				std::vector<PropertyInfo> list;
				node->get_property_list(&list);
				for (const PropertyInfo &pi : list) {
					if (pi.name == property && !(pi.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP))) {
						info = pi;
						break;
					}
				}
			}
			ImGui::PushID("key_value");
			ImGui::SetNextItemWidth(ES(240));
			if (_edit_property(node ? (Object *)node : (Object *)p_player, info, value)) {
				p_player->track_insert_key(anim_current, track, key_time, value);
				apply = true;
				_mark_modified();
			}
			ImGui::PopID();
			ImGui::SameLine();
			if (ImGui::Button("Supprimer la clé")) {
				p_player->track_remove_key(anim_current, track, anim_selected_key);
				anim_selected_key = -1;
				apply = true;
				_mark_modified();
			}
		}
	}

	if (apply) {
		_anim_preview_apply(p_player);
	}
}
