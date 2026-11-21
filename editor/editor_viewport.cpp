// Vue 2D (édition de la scène) et vue « Jeu » (scène en cours d'exécution).
#include "editor_node.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/resource_loader.h"
#include "core/string/ustring.h"
#include "scene/2d/node_2d.h"
#include "scene/2d/tile_map.h"
#include "scene/gui/control.h"
#include "scene/main/canvas_item.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/texture.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace {

const ImU32 COLOR_SELECTION = IM_COL32(255, 140, 40, 255);
const ImU32 COLOR_FRAME = IM_COL32(110, 150, 255, 200);

inline ImTextureID _texture_id(RID p_texture) {
	return (ImTextureID)(intptr_t)RenderingServer::get_singleton()->texture_get_native(p_texture);
}

// Transformation du calque (CanvasLayer) dans lequel se trouve l'élément.
Transform2D _layer_xform(const CanvasItem *p_item) {
	if (CanvasLayer *layer = p_item->get_canvas_layer_node()) {
		return layer->get_layer_transform();
	}
	return Transform2D();
}

// Repère local de l'élément -> monde de la vue d'édition.
Transform2D _item_world_xform(const CanvasItem *p_item) {
	return _layer_xform(p_item) * p_item->get_global_transform();
}

// Repère du parent -> monde (pour convertir un déplacement à l'écran en position locale).
Transform2D _parent_world_xform(const CanvasItem *p_item) {
	const CanvasItem *parent = Object::cast_to<CanvasItem>(p_item->get_parent());
	if (parent) {
		return _layer_xform(p_item) * parent->get_global_transform();
	}
	return _layer_xform(p_item);
}

Rect2 _item_rect(const CanvasItem *p_item, real_t p_min_size) {
	Rect2 rect = p_item->_edit_get_rect().abs();
	if (rect.size.x < p_min_size) {
		rect.position.x -= (p_min_size - rect.size.x) * 0.5f;
		rect.size.x = p_min_size;
	}
	if (rect.size.y < p_min_size) {
		rect.position.y -= (p_min_size - rect.size.y) * 0.5f;
		rect.size.y = p_min_size;
	}
	return rect;
}

} // namespace

Transform2D EditorNode::_get_view_transform() const {
	return Transform2D(0, Size2(view_zoom, view_zoom), -view_offset * view_zoom);
}

Vector2 EditorNode::_snap(const Vector2 &p_value) const {
	if (!snap_enabled || snap_step <= 0.0f) {
		return p_value;
	}
	return Vector2(std::round(p_value.x / snap_step) * snap_step, std::round(p_value.y / snap_step) * snap_step);
}

// ---------------------------------------------------------------------------
// Sélection à la souris
// ---------------------------------------------------------------------------
void EditorNode::_pick_recursive(Node *p_node, const Vector2 &p_world, bool p_with_rect, Node *&r_found) const {
	CanvasItem *item = Object::cast_to<CanvasItem>(p_node);
	if (item) {
		if (!item->is_visible()) {
			return; // les enfants d'un élément masqué sont masqués
		}
		const Transform2D xform = _item_world_xform(item);
		if (xform.determinant() != 0 && item->_edit_use_rect() == p_with_rect) {
			const Vector2 local = xform.affine_inverse().xform(p_world);
			const real_t scale = std::max((real_t)0.0001, std::sqrt(std::fabs(xform.determinant())));
			const Rect2 rect = _item_rect(item, 10.0f / (scale * view_zoom));
			if (rect.has_point(local)) {
				// L'intérieur d'une scène instanciée sélectionne l'instance elle-même.
				Node *target = p_node;
				while (target && !_is_editable(target)) {
					target = target->get_parent();
				}
				if (target) {
					r_found = target;
				}
			}
		}
	}
	for (Node *child : p_node->get_children_vector()) {
		_pick_recursive(child, p_world, p_with_rect, r_found);
	}
}

Node *EditorNode::_pick_node(const Vector2 &p_world) const {
	Node *found = nullptr;
	if (scene_root) {
		// Les éléments visibles (sprites, formes, contrôles...) sont prioritaires sur les nœuds
		// sans étendue (Node2D, Camera2D...), qui ne sont cliquables que par leur petite poignée.
		_pick_recursive(scene_root, p_world, true, found);
		if (!found) {
			_pick_recursive(scene_root, p_world, false, found);
		}
	}
	return found;
}

void EditorNode::_focus_selection() {
	Size2 view_size = edit_target_size;
	if (view_size.x < 32 || view_size.y < 32) {
		view_size = Size2(900, 560);
	}
	Rect2 rect;
	CanvasItem *item = Object::cast_to<CanvasItem>(_get_selected());
	if (item && item != scene_root) {
		rect = _item_world_xform(item).xform(_item_rect(item, 16));
		rect = rect.grow(std::max(rect.size.x, rect.size.y) * 0.5f + 32);
	} else {
		// Cadre du jeu (taille de la fenêtre du projet).
		rect = Rect2(0, 0, (real_t)GLOBAL_GET("display/width").as_int(), (real_t)GLOBAL_GET("display/height").as_int()).grow(48);
	}
	if (rect.size.x <= 0 || rect.size.y <= 0) {
		return;
	}
	view_zoom = (real_t)Math::clamp((double)std::min(view_size.x / rect.size.x, view_size.y / rect.size.y), 0.05, 8.0);
	view_offset = rect.get_center() - view_size / (2.0f * view_zoom);
	view_autofit = true;
	view_autofit_frames = 6;
}

// ---------------------------------------------------------------------------
// Rendu de la scène éditée dans sa texture
// ---------------------------------------------------------------------------
void EditorNode::_render_edit_view() {
	if (view_3d) {
		_render_edit_view_3d();
		return;
	}
	if (!viewport_visible || !edit_target.is_valid() || !edit_tree) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->set_render_target(edit_target);
	rs->clear(Color(0.13f, 0.14f, 0.17f));

	const Transform2D view = _get_view_transform();
	const Size2 size = edit_target_size;

	// Zone visible en jeu : remplie avec la couleur de fond du projet.
	const Size2 game_frame((real_t)GLOBAL_GET("display/width").as_int(), (real_t)GLOBAL_GET("display/height").as_int());
	const Rect2 frame_px(view.xform(Vector2()), game_frame * view_zoom);
	rs->draw_rect_immediate(frame_px, GLOBAL_GET("rendering/clear_color").as_color(), true);

	if (show_grid) {
		real_t step = std::max(4.0f, snap_step);
		for (int i = 0; i < 16 && step * view_zoom < 14.0f; i++) {
			step *= 2.0f;
		}
		const Vector2 world_begin = view_offset;
		const Vector2 world_end = view_offset + size / view_zoom;
		const Color minor(1, 1, 1, 0.045f);
		const Color major(1, 1, 1, 0.10f);
		for (real_t x = std::floor(world_begin.x / step) * step; x <= world_end.x; x += step) {
			const real_t px = std::round((x - view_offset.x) * view_zoom);
			const bool is_major = std::fmod(std::fabs(std::round(x / step)), 4.0) < 0.5;
			rs->draw_line_immediate(Vector2(px, 0), Vector2(px, size.y), is_major ? major : minor);
		}
		for (real_t y = std::floor(world_begin.y / step) * step; y <= world_end.y; y += step) {
			const real_t py = std::round((y - view_offset.y) * view_zoom);
			const bool is_major = std::fmod(std::fabs(std::round(y / step)), 4.0) < 0.5;
			rs->draw_line_immediate(Vector2(0, py), Vector2(size.x, py), is_major ? major : minor);
		}
	}
	// Axes du monde.
	const Vector2 origin_px = view.xform(Vector2());
	rs->draw_line_immediate(Vector2(0, origin_px.y), Vector2(size.x, origin_px.y), Color(0.9f, 0.3f, 0.3f, 0.55f));
	rs->draw_line_immediate(Vector2(origin_px.x, 0), Vector2(origin_px.x, size.y), Color(0.4f, 0.85f, 0.4f, 0.55f));

	edit_tree->draw(&view);

	rs->draw_rect_immediate(frame_px, Color(0.43f, 0.59f, 1.0f, 0.8f), false);
}

// ---------------------------------------------------------------------------
// Dock « Vue 2D »
// ---------------------------------------------------------------------------
void EditorNode::_draw_viewport_dock() {
	viewport_visible = false;
	if (focus_viewport) {
		focus_viewport = false;
		ImGui::SetNextWindowFocus();
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
	_update_view_mode();
	const bool visible = ImGui::Begin(view_3d ? "Vue 3D###Viewport" : "Vue 2D###Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	if (!visible) {
		ImGui::End();
		if (drag != DRAG_NONE) {
			drag = DRAG_NONE;
		}
		drag3d = DRAG3D_NONE;
		return;
	}

	// --- Choix de la vue : 2D ou 3D (suit automatiquement le type du nœud sélectionné) ---
	for (int mode = 0; mode < 2; mode++) {
		const bool active = view_3d == (mode == 1);
		if (active) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button(mode == 0 ? "2D" : "3D")) {
			view_3d = mode == 1;
			drag = DRAG_NONE;
			drag3d = DRAG3D_NONE;
		}
		if (active) {
			ImGui::PopStyleColor();
		}
		ImGui::SetItemTooltip("%s", mode == 0 ? "Vue 2D : canvas, sprites, interface" : "Vue 3D : maillages, lumières, caméras");
		ImGui::SameLine();
	}
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine();
	if (view_3d) {
		tile_paint = false;
		_draw_viewport_3d();
		ImGui::End();
		return;
	}

	// --- Barre d'outils ---
	auto tool_button = [this](const char *p_label, Tool p_tool, const char *p_tip) {
		const bool active = tool == p_tool && !tile_paint;
		if (active) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button(p_label)) {
			tool = p_tool;
			tile_paint = false;
		}
		if (active) {
			ImGui::PopStyleColor();
		}
		ImGui::SetItemTooltip("%s", p_tip);
		ImGui::SameLine();
	};
	tool_button("Déplacer", TOOL_SELECT, "Sélectionner et déplacer (Q)");
	tool_button("Pivoter", TOOL_ROTATE, "Faire pivoter le nœud sélectionné (E)\nCtrl : pas de 15°");
	tool_button("Échelle", TOOL_SCALE, "Redimensionner le nœud sélectionné (R)\nMaj : conserver les proportions");

	TileMap *tile_map = Object::cast_to<TileMap>(_get_selected());
	if (tile_map) {
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();
		if (tile_paint) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		const bool was_painting = tile_paint;
		if (ImGui::Button("Peindre les tuiles")) {
			tile_paint = !tile_paint;
		}
		if (was_painting) {
			ImGui::PopStyleColor();
		}
		ImGui::SetItemTooltip("Clic gauche : poser une tuile\nClic droit : effacer");
		ImGui::SameLine();
	} else {
		tile_paint = false;
	}

	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine();
	ImGui::Checkbox("Grille", &show_grid);
	ImGui::SameLine();
	ImGui::Checkbox("Aimant", &snap_enabled);
	ImGui::SetItemTooltip("Aimanter les déplacements à la grille");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(70);
	if (ImGui::DragFloat("##snap", &snap_step, 0.5f, 1.0f, 512.0f, "%.0f px")) {
		snap_step = std::max(1.0f, snap_step);
	}
	ImGui::SetItemTooltip("Pas de la grille");
	ImGui::SameLine();
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine();
	if (ImGui::Button("Centrer")) {
		_focus_selection();
	}
	ImGui::SetItemTooltip("Centrer la vue sur la sélection (F)");
	ImGui::SameLine();
	if (ImGui::Button("1:1")) {
		const Vector2 center = view_offset + edit_target_size / (2.0f * view_zoom);
		view_zoom = 1.0f;
		view_offset = center - edit_target_size / 2.0f;
		view_autofit = false;
	}
	ImGui::SetItemTooltip("Zoom 100 %%");
	ImGui::SameLine();
	ImGui::TextDisabled("%d %%", (int)std::round(view_zoom * 100.0f));

	// --- Palette de tuiles (à droite) ---
	const float palette_width = (tile_map && tile_paint) ? 196.0f : 0.0f;

	// --- Zone de dessin ---
	ImVec2 avail = ImGui::GetContentRegionAvail();
	avail.x -= palette_width > 0 ? palette_width + ImGui::GetStyle().ItemSpacing.x : 0;
	const Size2 size(std::max(16.0f, std::floor(avail.x)), std::max(16.0f, std::floor(avail.y)));
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!edit_target.is_valid() || edit_target_size != size) {
		const bool first = !edit_target.is_valid();
		if (edit_target.is_valid()) {
			rs->free_rid(edit_target);
		}
		edit_target = rs->texture_create_target((int)size.x, (int)size.y);
		edit_target_size = size;
		if (first || view_autofit) {
			_focus_selection();
		}
	}
	viewport_visible = true;
	if (view_autofit && --view_autofit_frames <= 0) {
		view_autofit = false;
	}

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##canvas", ImVec2(size.x, size.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	ImDrawList *draw_list = ImGui::GetWindowDrawList();
	draw_list->AddImage(_texture_id(edit_target), origin, ImVec2(origin.x + size.x, origin.y + size.y));

	// Dépôt d'un fichier dans la vue : instancie une scène ou crée un Sprite2D.
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MEX_FILE")) {
			const String path((const char *)payload->Data);
			const Vector2 mouse(ImGui::GetMousePos().x - origin.x, ImGui::GetMousePos().y - origin.y);
			const Vector2 world = _snap(view_offset + mouse / view_zoom);
			_defer([this, path, world]() {
				if (!scene_root) {
					print_warning("Créez d'abord un nœud racine avant de déposer un fichier dans la vue.");
					return;
				}
				const String ext = path_get_extension(path);
				Node *created = nullptr;
				if (ext == "scn") {
					created = _instance_scene(path, scene_root);
				} else if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp") {
					created = _create_node("Sprite2D", scene_root);
					if (created) {
						Ref<Resource> texture = ResourceLoader::load(path);
						created->set("texture", Variant(texture));
						created->set_name(str_capitalize(path_get_basename(path_get_file(path))));
					}
				}
				if (CanvasItem *item = Object::cast_to<CanvasItem>(created)) {
					const Transform2D parent = _parent_world_xform(item);
					if (parent.determinant() != 0) {
						item->set("position", Variant(parent.affine_inverse().xform(world)));
					}
					_mark_modified();
				}
			});
		}
		ImGui::EndDragDropTarget();
	}

	_viewport_input(Vector2(origin.x, origin.y), size, hovered);
	draw_list->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	_viewport_overlay(Vector2(origin.x, origin.y), size);
	draw_list->PopClipRect();

	if (palette_width > 0) {
		ImGui::SameLine();
		ImGui::BeginChild("##palette", ImVec2(palette_width, size.y), ImGuiChildFlags_Borders);
		_draw_tile_palette(tile_map);
		ImGui::EndChild();
	}
	ImGui::End();
}

void EditorNode::_viewport_input(const Vector2 &p_origin, const Size2 &p_size, bool p_hovered) {
	ImGuiIO &io = ImGui::GetIO();
	const Vector2 mouse(io.MousePos.x - p_origin.x, io.MousePos.y - p_origin.y);
	const Vector2 world = view_offset + mouse / view_zoom;
	if (p_hovered || drag != DRAG_NONE) {
		viewport_mouse_world = world;
	}
	const bool window_focused_now = ImGui::IsWindowFocused();

	// Zoom centré sur le curseur.
	if (p_hovered && io.MouseWheel != 0.0f) {
		const real_t factor = std::pow(1.15f, io.MouseWheel);
		const real_t new_zoom = (real_t)Math::clamp((double)(view_zoom * factor), 0.05, 32.0);
		view_offset = world - mouse / new_zoom;
		view_zoom = new_zoom;
		view_autofit = false;
	}

	// Raccourcis clavier de la vue.
	if ((p_hovered || window_focused_now) && !io.WantTextInput && !io.KeyCtrl && drag == DRAG_NONE) {
		if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
			tool = TOOL_SELECT;
			tile_paint = false;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
			tool = TOOL_ROTATE;
			tile_paint = false;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			tool = TOOL_SCALE;
			tile_paint = false;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			_focus_selection();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			if (tile_paint) {
				tile_paint = false;
			} else {
				_select(nullptr);
			}
		}
		Node *selected = _get_selected();
		if (selected && window_focused_now) {
			if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
				_defer([this, selected]() { _delete_node(selected); });
			}
			// Flèches : déplacement fin (Maj : d'un pas de grille).
			Vector2 nudge;
			nudge.x = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1.0f : 0.0f) - (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? 1.0f : 0.0f);
			nudge.y = (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1.0f : 0.0f) - (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? 1.0f : 0.0f);
			CanvasItem *item = Object::cast_to<CanvasItem>(selected);
			if (item && (nudge.x != 0 || nudge.y != 0)) {
				if (io.KeyShift) {
					nudge = nudge * snap_step;
				}
				item->set("position", Variant(item->get("position").as_vector2() + nudge));
				_mark_modified();
			}
		}
	}

	// --- Déplacement de la vue ---
	const bool pan_pressed = p_hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !tile_paint) || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsKeyDown(ImGuiKey_Space)));
	if (drag == DRAG_NONE && pan_pressed) {
		drag = DRAG_PAN;
	}
	if (drag == DRAG_PAN) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			view_offset -= Vector2(io.MouseDelta.x, io.MouseDelta.y) / view_zoom;
			view_autofit = false;
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		} else {
			drag = DRAG_NONE;
		}
		return;
	}

	// --- Peinture de tuiles ---
	TileMap *tile_map = tile_paint ? Object::cast_to<TileMap>(_get_selected()) : nullptr;
	if (tile_map) {
		if (drag == DRAG_NONE && p_hovered) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				drag = DRAG_PAINT;
			} else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				drag = DRAG_ERASE;
			}
			tile_has_last_cell = false;
		}
		if (drag == DRAG_PAINT || drag == DRAG_ERASE) {
			const bool down = ImGui::IsMouseDown(drag == DRAG_PAINT ? ImGuiMouseButton_Left : ImGuiMouseButton_Right);
			if (!down) {
				drag = DRAG_NONE;
				tile_has_last_cell = false;
			} else {
				const Transform2D xform = _item_world_xform(tile_map);
				if (xform.determinant() != 0) {
					const Vector2 cell = tile_map->local_to_map(xform.affine_inverse().xform(world));
					// Trace une ligne depuis la cellule précédente : pas de trous si la souris va vite.
					const Vector2 from = tile_has_last_cell ? tile_last_cell : cell;
					const int steps = std::min(512, (int)std::max(std::fabs(cell.x - from.x), std::fabs(cell.y - from.y)));
					for (int i = 0; i <= steps; i++) {
						const real_t t = steps > 0 ? (real_t)i / (real_t)steps : 1.0f;
						const int cx = (int)std::round(from.x + (cell.x - from.x) * t);
						const int cy = (int)std::round(from.y + (cell.y - from.y) * t);
						if (drag == DRAG_PAINT) {
							if (tile_map->get_cell(cx, cy) != tile_selected) {
								tile_map->set_cell(cx, cy, tile_selected);
								_mark_modified();
							}
						} else if (tile_map->get_cell(cx, cy) != -1) {
							tile_map->erase_cell(cx, cy);
							_mark_modified();
						}
					}
					tile_last_cell = cell;
					tile_has_last_cell = true;
				}
			}
		}
		return;
	}
	if (drag == DRAG_PAINT || drag == DRAG_ERASE) {
		drag = DRAG_NONE;
	}

	// --- Sélection et manipulation ---
	if (drag == DRAG_NONE && p_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		Node *selected = _get_selected();
		CanvasItem *selected_item = Object::cast_to<CanvasItem>(selected);
		Node *target = nullptr;

		// Avec les outils Pivoter/Échelle, on manipule la sélection courante où que l'on clique.
		if (tool != TOOL_SELECT && selected_item) {
			target = selected;
		} else {
			// Un clic dans le rectangle de la sélection la conserve (même si un autre nœud la recouvre).
			if (selected_item && selected_item->is_visible()) {
				const Transform2D xform = _item_world_xform(selected_item);
				if (xform.determinant() != 0) {
					const real_t scale = std::max((real_t)0.0001, std::sqrt(std::fabs(xform.determinant())));
					if (_item_rect(selected_item, 10.0f / (scale * view_zoom)).has_point(xform.affine_inverse().xform(world)) && !io.KeyAlt) {
						target = selected;
					}
				}
			}
			if (!target) {
				target = _pick_node(world);
				_select(target);
			}
		}

		CanvasItem *item = Object::cast_to<CanvasItem>(target);
		if (item) {
			drag_start_world = world;
			if (tool == TOOL_SELECT) {
				drag = DRAG_MOVE;
				drag_start_value = item->get("position").as_vector2();
			} else if (tool == TOOL_ROTATE && Object::cast_to<Node2D>(item)) {
				drag = DRAG_ROTATE;
				drag_start_rotation = Object::cast_to<Node2D>(item)->get_rotation();
			} else if (tool == TOOL_SCALE) {
				drag = DRAG_SCALE;
				if (Node2D *n2d = Object::cast_to<Node2D>(item)) {
					drag_start_value = n2d->get_scale();
				} else {
					drag_start_value = item->get("size").as_vector2();
				}
			}
		}
	}

	if (drag == DRAG_MOVE || drag == DRAG_ROTATE || drag == DRAG_SCALE) {
		CanvasItem *item = Object::cast_to<CanvasItem>(_get_selected());
		if (!item || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			drag = DRAG_NONE;
			return;
		}
		const Vector2 delta = world - drag_start_world;
		const Transform2D parent = _parent_world_xform(item);
		const Vector2 pivot = _item_world_xform(item).origin;
		switch (drag) {
			case DRAG_MOVE: {
				if (parent.determinant() == 0 || (delta.x == 0 && delta.y == 0 && !scene_modified && !action_dirty)) {
					break;
				}
				// Un simple clic ne doit pas modifier la scène : seuil de 3 pixels.
				if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
					break;
				}
				Vector2 local_delta = parent.affine_inverse().basis_xform(delta);
				if (io.KeyShift) {
					// Maj : contraint le déplacement à un axe.
					if (std::fabs(local_delta.x) > std::fabs(local_delta.y)) {
						local_delta.y = 0;
					} else {
						local_delta.x = 0;
					}
				}
				const Vector2 new_position = _snap(drag_start_value + local_delta);
				if (new_position != item->get("position").as_vector2()) {
					item->set("position", Variant(new_position));
					_mark_modified();
				}
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
			} break;
			case DRAG_ROTATE: {
				Node2D *n2d = Object::cast_to<Node2D>(item);
				const Vector2 from = drag_start_world - pivot;
				const Vector2 to = world - pivot;
				if (!n2d || from.length() < 0.001f || to.length() < 0.001f) {
					break;
				}
				real_t angle = std::atan2(to.y, to.x) - std::atan2(from.y, from.x);
				real_t rotation = drag_start_rotation + angle;
				if (io.KeyCtrl) {
					const real_t step = (real_t)Math::deg_to_rad(15.0);
					rotation = std::round(rotation / step) * step;
				}
				if (rotation != n2d->get_rotation()) {
					n2d->set_rotation(rotation);
					_mark_modified();
				}
			} break;
			case DRAG_SCALE: {
				if (Node2D *n2d = Object::cast_to<Node2D>(item)) {
					const Vector2 from = drag_start_world - pivot;
					const Vector2 to = world - pivot;
					Vector2 factor(1, 1);
					if (io.KeyShift || std::fabs(from.x) < 2.0f / view_zoom || std::fabs(from.y) < 2.0f / view_zoom) {
						const real_t f = from.length() > 0.001f ? to.length() / from.length() : 1.0f;
						factor = Vector2(f, f);
					} else {
						// Facteurs indépendants par axe, dans le repère du nœud.
						const real_t rot = _item_world_xform(item).get_rotation();
						const Vector2 lf = from.rotated(-rot);
						const Vector2 lt = to.rotated(-rot);
						factor = Vector2(std::fabs(lf.x) > 0.001f ? lt.x / lf.x : 1.0f, std::fabs(lf.y) > 0.001f ? lt.y / lf.y : 1.0f);
					}
					Vector2 new_scale = drag_start_value * factor;
					new_scale.x = std::round(new_scale.x * 1000.0f) / 1000.0f;
					new_scale.y = std::round(new_scale.y * 1000.0f) / 1000.0f;
					if (new_scale != n2d->get_scale()) {
						n2d->set_scale(new_scale);
						_mark_modified();
					}
				} else if (parent.determinant() != 0) {
					// Control : l'outil modifie la taille.
					const Vector2 local_delta = _item_world_xform(item).affine_inverse().basis_xform(delta);
					Vector2 new_size = _snap(drag_start_value + local_delta);
					new_size.x = std::max((real_t)0, new_size.x);
					new_size.y = std::max((real_t)0, new_size.y);
					if (new_size != item->get("size").as_vector2()) {
						item->set("size", Variant(new_size));
						_mark_modified();
					}
				}
			} break;
			default:
				break;
		}
	}
	(void)p_size;
}

void EditorNode::_viewport_overlay(const Vector2 &p_origin, const Size2 &p_size) {
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const Transform2D view = _get_view_transform();
	auto to_screen = [&](const Vector2 &p_world) {
		const Vector2 p = view.xform(p_world);
		return ImVec2(std::round(p_origin.x + p.x), std::round(p_origin.y + p.y));
	};

	Node *selected = _get_selected();
	CanvasItem *item = Object::cast_to<CanvasItem>(selected);
	if (item) {
		const Transform2D xform = _item_world_xform(item);
		const real_t scale = std::max((real_t)0.0001, std::sqrt(std::fabs(xform.determinant())));
		const Rect2 rect = _item_rect(item, item->_edit_use_rect() ? 0.0f : 16.0f / (scale * view_zoom));
		const ImVec2 corners[4] = {
			to_screen(xform.xform(rect.position)),
			to_screen(xform.xform(Vector2(rect.position.x + rect.size.x, rect.position.y))),
			to_screen(xform.xform(rect.position + rect.size)),
			to_screen(xform.xform(Vector2(rect.position.x, rect.position.y + rect.size.y))),
		};
		dl->AddPolyline(corners, 4, COLOR_SELECTION, ImDrawFlags_Closed, 1.5f);
		for (const ImVec2 &corner : corners) {
			dl->AddRectFilled(ImVec2(corner.x - 3, corner.y - 3), ImVec2(corner.x + 3, corner.y + 3), COLOR_SELECTION);
		}
		// Pivot (origine du nœud).
		const ImVec2 pivot = to_screen(xform.origin);
		dl->AddLine(ImVec2(pivot.x - 7, pivot.y), ImVec2(pivot.x + 7, pivot.y), IM_COL32(255, 255, 255, 220), 1.5f);
		dl->AddLine(ImVec2(pivot.x, pivot.y - 7), ImVec2(pivot.x, pivot.y + 7), IM_COL32(255, 255, 255, 220), 1.5f);

		if (drag == DRAG_ROTATE || drag == DRAG_SCALE) {
			dl->AddLine(pivot, ImGui::GetIO().MousePos, IM_COL32(255, 255, 255, 110), 1.0f);
		}
		if (drag == DRAG_ROTATE) {
			if (Node2D *n2d = Object::cast_to<Node2D>(item)) {
				const String text = vformat("%.1f°", (double)n2d->get_rotation_degrees());
				dl->AddText(ImVec2(pivot.x + 12, pivot.y - 22), IM_COL32_WHITE, text.c_str());
			}
		} else if (drag == DRAG_MOVE) {
			const Vector2 pos = item->get("position").as_vector2();
			const String text = vformat("(%.0f, %.0f)", (double)pos.x, (double)pos.y);
			dl->AddText(ImVec2(pivot.x + 12, pivot.y - 22), IM_COL32_WHITE, text.c_str());
		} else if (drag == DRAG_SCALE) {
			const Vector2 value = Object::cast_to<Node2D>(item) ? Object::cast_to<Node2D>(item)->get_scale() : item->get("size").as_vector2();
			const String text = vformat("(%.2f, %.2f)", (double)value.x, (double)value.y);
			dl->AddText(ImVec2(pivot.x + 12, pivot.y - 22), IM_COL32_WHITE, text.c_str());
		}
	}

	// Cellule survolée en mode peinture.
	TileMap *tile_map = tile_paint ? Object::cast_to<TileMap>(selected) : nullptr;
	if (tile_map) {
		const Transform2D xform = _item_world_xform(tile_map);
		if (xform.determinant() != 0) {
			const Vector2 tile_size = tile_map->get_tile_size();
			const Vector2 cell = tile_map->local_to_map(xform.affine_inverse().xform(viewport_mouse_world));
			const Vector2 p0 = cell * tile_size;
			const ImVec2 corners[4] = {
				to_screen(xform.xform(p0)),
				to_screen(xform.xform(p0 + Vector2(tile_size.x, 0))),
				to_screen(xform.xform(p0 + tile_size)),
				to_screen(xform.xform(p0 + Vector2(0, tile_size.y))),
			};
			dl->AddPolyline(corners, 4, IM_COL32(255, 255, 255, 230), ImDrawFlags_Closed, 1.5f);
			const String text = vformat("(%d, %d)", (int)cell.x, (int)cell.y);
			dl->AddText(ImVec2(corners[0].x + 4, corners[0].y - 18), IM_COL32_WHITE, text.c_str());
		}
	}

	// Légende du cadre de jeu.
	const ImVec2 frame_origin = to_screen(Vector2());
	const String frame_text = vformat("Écran du jeu  %d x %d", (int)GLOBAL_GET("display/width").as_int(), (int)GLOBAL_GET("display/height").as_int());
	dl->AddText(ImVec2(frame_origin.x + 2, frame_origin.y - 18), COLOR_FRAME, frame_text.c_str());

	// Position de la souris (monde).
	const String status = vformat("x %.0f   y %.0f", (double)viewport_mouse_world.x, (double)viewport_mouse_world.y);
	if (ImGui::IsItemHovered() || drag != DRAG_NONE) {
		dl->AddText(ImVec2(p_origin.x + 8, p_origin.y + p_size.y - 22), IM_COL32(200, 205, 215, 190), status.c_str());
	}

	if (!scene_root) {
		const char *hint = "Scène vide : ajoutez un nœud racine depuis le dock « Scène ».";
		const ImVec2 text_size = ImGui::CalcTextSize(hint);
		dl->AddText(ImVec2(p_origin.x + (p_size.x - text_size.x) * 0.5f, p_origin.y + p_size.y * 0.5f), IM_COL32(200, 205, 215, 200), hint);
	}
}

void EditorNode::_draw_tile_palette(TileMap *p_map) {
	ImGui::TextUnformatted("Tuiles");
	Ref<Texture2D> texture = p_map->get_texture();
	const int count = p_map->get_atlas_tile_count();
	const Vector2 tile_size = p_map->get_tile_size();
	if (texture.is_null()) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("Aucune texture : tuiles de couleur. Assignez une image d'atlas à la propriété « texture ».");
		ImGui::PopTextWrapPos();
	}
	ImGui::Separator();
	tile_selected = std::clamp(tile_selected, 0, std::max(0, count - 1));

	const float button = 40.0f;
	const int per_row = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 4) / (button + 4)));
	ImDrawList *dl = ImGui::GetWindowDrawList();
	for (int i = 0; i < count; i++) {
		ImGui::PushID(i);
		if (i % per_row != 0) {
			ImGui::SameLine(0, 4);
		}
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		if (ImGui::InvisibleButton("##tile", ImVec2(button, button))) {
			tile_selected = i;
		}
		const ImVec2 end(pos.x + button, pos.y + button);
		if (texture.is_valid() && texture->get_width() > 0 && texture->get_height() > 0) {
			const Rect2 region = p_map->get_tile_region(i);
			const Size2 tex_size = texture->get_size();
			dl->AddRectFilled(pos, end, IM_COL32(40, 44, 52, 255));
			dl->AddImage(_texture_id(texture->get_rid()), pos, end, ImVec2(region.position.x / tex_size.x, region.position.y / tex_size.y), ImVec2((region.position.x + region.size.x) / tex_size.x, (region.position.y + region.size.y) / tex_size.y));
		} else {
			const Color c = TileMap::get_placeholder_color(i);
			dl->AddRectFilled(pos, end, IM_COL32((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), 255));
		}
		if (i == tile_selected) {
			dl->AddRect(ImVec2(pos.x - 1, pos.y - 1), ImVec2(end.x + 1, end.y + 1), COLOR_SELECTION, 0.0f, 0, 2.5f);
		} else if (ImGui::IsItemHovered()) {
			dl->AddRect(pos, end, IM_COL32(255, 255, 255, 160));
		}
		ImGui::SetItemTooltip("Tuile %d", i);
		ImGui::PopID();
	}
	ImGui::Spacing();
	ImGui::TextDisabled("Tuile %d  (%d x %d px)", tile_selected, (int)tile_size.x, (int)tile_size.y);
	ImGui::TextDisabled("Gauche : poser\nDroit : effacer\nMolette : zoom\nMilieu : déplacer la vue");
	if (ImGui::Button("Tout effacer", ImVec2(-FLT_MIN, 0))) {
		const ObjectID id = p_map->get_instance_id();
		_confirm("Effacer les tuiles", "Effacer toutes les tuiles de « " + p_map->get_name() + " » ?", {
																											 { "Effacer", [this, id]() {
																												  if (TileMap *map = Object::cast_to<TileMap>(ObjectDB::get_instance(id))) {
																													  map->clear();
																													  _mark_modified();
																												  }
																											  } },
																											 { "Annuler", nullptr },
																									 });
	}
}

// ---------------------------------------------------------------------------
// Dock « Jeu »
// ---------------------------------------------------------------------------
void EditorNode::_draw_game_dock() {
	game_view_focused = false;
	game_view_hovered = false;
	if (focus_game) {
		focus_game = false;
		ImGui::SetNextWindowFocus();
	}
	if (!ImGui::Begin("Jeu###Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav)) {
		ImGui::End();
		return;
	}
	if (!game.is_running()) {
		ImGui::Spacing();
		ImGui::TextDisabled("Le jeu n'est pas lancé.");
		ImGui::Spacing();
		if (ImGui::Button("Lancer le projet (F5)", ImVec2(240, 0))) {
			_defer([this]() { _play_main(); });
		}
		if (ImGui::Button("Lancer la scène courante (F6)", ImVec2(240, 0))) {
			_defer([this]() { _play_current(); });
		}
		ImGui::End();
		return;
	}

	if (ImGui::Button(game_paused ? "Reprendre" : "Pause")) {
		game_paused = !game_paused;
		if (game_paused) {
			Input::get_singleton()->release_all();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Arrêter (F8)")) {
		_defer([this]() { _stop(); });
	}
	ImGui::SameLine();
	if (ImGui::Button("Relancer")) {
		const String scene = game.get_scene_path();
		_defer([this, scene]() { _play(scene); });
	}
	ImGui::SameLine();
	RenderingServer *rs = RenderingServer::get_singleton();
	ImGui::TextDisabled("%s   %.0f i/s   %d éléments dessinés", game.get_scene_path().c_str(), (double)ImGui::GetIO().Framerate, rs->get_stat_items());

	// Image du jeu, mise à l'échelle en conservant les proportions.
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const ImVec2 base = ImGui::GetCursorScreenPos();
	if (avail.x > 8 && avail.y > 8 && game_size.x > 0 && game_size.y > 0 && game_target.is_valid()) {
		real_t scale = std::min(avail.x / game_size.x, avail.y / game_size.y);
		if (scale > 1.0f) {
			scale = std::floor(scale); // agrandissement entier : pixels nets
		}
		const Size2 image_size(std::floor(game_size.x * scale), std::floor(game_size.y * scale));
		const ImVec2 pos(std::floor(base.x + (avail.x - image_size.x) * 0.5f), std::floor(base.y + (avail.y - image_size.y) * 0.5f));
		ImGui::InvisibleButton("##game", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		game_view_hovered = ImGui::IsItemHovered() && mouse.x >= pos.x && mouse.y >= pos.y && mouse.x < pos.x + image_size.x && mouse.y < pos.y + image_size.y;
		game_view_focused = ImGui::IsWindowFocused();
		game_image_pos = Vector2(pos.x, pos.y);
		game_image_size = image_size;
		ImDrawList *dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(base, ImVec2(base.x + avail.x, base.y + avail.y), IM_COL32(12, 13, 16, 255));
		dl->AddImage(_texture_id(game_target), pos, ImVec2(pos.x + image_size.x, pos.y + image_size.y));
		dl->AddRect(ImVec2(pos.x - 1, pos.y - 1), ImVec2(pos.x + image_size.x + 1, pos.y + image_size.y + 1), game_view_focused ? IM_COL32(110, 150, 255, 255) : IM_COL32(70, 76, 88, 255));
		if (game_paused) {
			dl->AddRectFilled(pos, ImVec2(pos.x + image_size.x, pos.y + image_size.y), IM_COL32(0, 0, 0, 110));
			const char *text = "EN PAUSE";
			const ImVec2 text_size = ImGui::CalcTextSize(text);
			dl->AddText(ImVec2(pos.x + (image_size.x - text_size.x) * 0.5f, pos.y + (image_size.y - text_size.y) * 0.5f), IM_COL32_WHITE, text);
		} else if (!game_view_focused) {
			const char *text = "Cliquez dans la vue pour envoyer le clavier au jeu";
			const ImVec2 text_size = ImGui::CalcTextSize(text);
			dl->AddText(ImVec2(pos.x + (image_size.x - text_size.x) * 0.5f, pos.y + image_size.y - text_size.y - 8), IM_COL32(255, 255, 255, 170), text);
		}
	}
	ImGui::End();
}
