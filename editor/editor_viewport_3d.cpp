// Vue 3D de l'éditeur : caméra orbitale, sélection au rayon, manipulateurs de
// déplacement / rotation / échelle et aides visuelles (grille, caméras, lumières, formes).
#include "editor_node.h"

#include "scene/resources/gltf_loader.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/string/ustring.h"
#include "scene/3d/collision_shape_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/visual_3d.h"
#include "scene/main/canvas_item.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/shape_3d.h"

#include "editor_ui.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>

namespace {

const real_t VIEW_FOV = 60.0f;
const float GIZMO_PIXELS = 86.0f; // longueur des axes du manipulateur à l'écran
const float GIZMO_PICK_PIXELS = 8.0f;
const ImU32 AXIS_COLORS[3] = { IM_COL32(235, 80, 80, 255), IM_COL32(110, 210, 80, 255), IM_COL32(80, 140, 245, 255) };
const ImU32 AXIS_HIGHLIGHT = IM_COL32(255, 225, 90, 255);

inline ImTextureID _texture_id(RID p_texture) {
	return (ImTextureID)(intptr_t)RenderingServer::get_singleton()->texture_get_native(p_texture);
}

float _distance_to_segment(const Vector2 &p, const Vector2 &a, const Vector2 &b) {
	const Vector2 ab = b - a;
	const float len2 = ab.length_squared();
	float t = len2 > 1e-6f ? (p - a).dot(ab) / len2 : 0.0f;
	t = Math::clamp(t, 0.0f, 1.0f);
	return (p - (a + ab * t)).length();
}

// Paramètre t du point de la droite (p_origin + t * p_axis) le plus proche du rayon.
bool _closest_on_axis(const Vector3 &p_origin, const Vector3 &p_axis, const Vector3 &p_ray_origin, const Vector3 &p_ray_dir, real_t &r_t) {
	const Vector3 w = p_origin - p_ray_origin;
	const real_t a = p_axis.dot(p_axis), b = p_axis.dot(p_ray_dir), c = p_ray_dir.dot(p_ray_dir);
	const real_t d = p_axis.dot(w), e = p_ray_dir.dot(w);
	const real_t denom = a * c - b * b;
	if (std::fabs(denom) < 1e-6f) {
		return false; // axe parallèle au rayon
	}
	r_t = (b * e - c * d) / denom;
	return true;
}

bool _ray_plane(const Vector3 &p_ray_origin, const Vector3 &p_ray_dir, const Vector3 &p_point, const Vector3 &p_normal, Vector3 &r_hit) {
	const real_t denom = p_ray_dir.dot(p_normal);
	if (std::fabs(denom) < 1e-5f) {
		return false;
	}
	const real_t t = (p_point - p_ray_origin).dot(p_normal) / denom;
	if (t < 0) {
		return false;
	}
	r_hit = p_ray_origin + p_ray_dir * t;
	return true;
}

Vector3 _round3(const Vector3 &v) {
	return Vector3(std::round(v.x * 1000.0f) / 1000.0f, std::round(v.y * 1000.0f) / 1000.0f, std::round(v.z * 1000.0f) / 1000.0f);
}

// Boîte englobante d'un nœud dans le monde (récursive). Retourne faux si rien n'a d'étendue.
bool _world_aabb(Node *p_node, AABB &r_aabb, bool &r_first) {
	bool any = false;
	if (Node3D *n = Object::cast_to<Node3D>(p_node)) {
		AABB local;
		if (n->_edit_get_aabb(local)) {
			const AABB world = local.transformed(n->get_global_transform());
			r_aabb = r_first ? world : r_aabb.merge(world);
			r_first = false;
			any = true;
		}
	}
	for (Node *child : p_node->get_children_vector()) {
		any = _world_aabb(child, r_aabb, r_first) || any;
	}
	return any;
}

} // namespace

bool EditorNode::_get_world_aabb(Node *p_node, AABB &r_aabb) {
	bool first = true;
	return _world_aabb(p_node, r_aabb, first);
}

// ---------------------------------------------------------------------------
// Choix automatique du mode 2D / 3D selon le nœud sélectionné
// ---------------------------------------------------------------------------
void EditorNode::_update_view_mode() {
	Node *reference = _get_selected();
	if (!reference) {
		reference = scene_root;
	}
	const ObjectID id = reference ? reference->get_instance_id() : 0;
	if (id == view_mode_reference) {
		return;
	}
	view_mode_reference = id;
	if (Object::cast_to<Node3D>(reference)) {
		view_3d = true;
	} else if (Object::cast_to<CanvasItem>(reference)) {
		view_3d = false;
	}
}

// ---------------------------------------------------------------------------
// Caméra de la vue
// ---------------------------------------------------------------------------
RenderingServer3D::CameraData EditorNode::_get_camera_3d() const {
	RenderingServer3D::CameraData cd;
	cd.transform.basis = Basis::from_euler(Vector3(cam3d_pitch, cam3d_yaw, 0));
	cd.transform.origin = cam3d_target + cd.transform.basis.get_column(2) * cam3d_distance;
	cd.orthogonal = cam3d_orthogonal;
	cd.fov = VIEW_FOV;
	cd.size = cam3d_distance; // hauteur visible en projection orthogonale
	cd.z_near = cam3d_orthogonal ? 0.01f : std::max(0.02f, cam3d_distance * 0.005f);
	cd.z_far = std::max(2000.0f, cam3d_distance * 20.0f);
	if (cam3d_orthogonal) {
		// Recule la caméra : en vue orthogonale la distance ne change pas l'image, mais la
		// scène ne doit pas être coupée par le plan proche.
		cd.transform.origin = cam3d_target + cd.transform.basis.get_column(2) * std::max(cam3d_distance, (real_t)500.0);
	}
	return cd;
}

void EditorNode::_get_mouse_ray_3d(const Vector2 &p_mouse, const Size2 &p_size, Vector3 &r_origin, Vector3 &r_direction) const {
	const RenderingServer3D::CameraData cd = _get_camera_3d();
	const real_t aspect = p_size.y > 0 ? p_size.x / p_size.y : 1.0f;
	const Vector2 ndc(p_mouse.x / std::max(p_size.x, 1.0f) * 2.0f - 1.0f, 1.0f - p_mouse.y / std::max(p_size.y, 1.0f) * 2.0f);
	if (cd.orthogonal) {
		r_origin = cd.transform.xform(Vector3(ndc.x * cd.size * 0.5f * aspect, ndc.y * cd.size * 0.5f, 0));
		r_direction = -cd.transform.basis.get_column(2);
	} else {
		const real_t half = std::tan(Math::deg_to_rad(cd.fov) * 0.5f);
		r_origin = cd.transform.origin;
		r_direction = cd.transform.basis.xform(Vector3(ndc.x * half * aspect, ndc.y * half, -1)).normalized();
	}
}

bool EditorNode::_project_3d(const Vector3 &p_world, const Size2 &p_size, Vector2 &r_screen) const {
	const RenderingServer3D::CameraData cd = _get_camera_3d();
	const Vector3 view = cd.transform.affine_inverse().xform(p_world);
	if (!cd.orthogonal && view.z > -cd.z_near) {
		return false; // derrière la caméra
	}
	const real_t aspect = p_size.y > 0 ? p_size.x / p_size.y : 1.0f;
	const Vector3 ndc = cd.get_projection(aspect).xform(view);
	r_screen = Vector2((ndc.x * 0.5f + 0.5f) * p_size.x, (0.5f - ndc.y * 0.5f) * p_size.y);
	return true;
}

void EditorNode::_focus_selection_3d() {
	Node *node = _get_selected();
	if (!node) {
		node = scene_root;
	}
	if (!node) {
		cam3d_target = Vector3(0, 0.5f, 0);
		cam3d_distance = 12.0f;
		return;
	}
	AABB box;
	bool first = true;
	if (_world_aabb(node, box, first)) {
		cam3d_target = box.get_center();
		cam3d_distance = Math::clamp(box.size.length() * 0.95f, (real_t)1.5, (real_t)2000.0);
	} else if (Node3D *n = Object::cast_to<Node3D>(node)) {
		cam3d_target = n->get_global_transform().origin;
		cam3d_distance = 6.0f;
	}
}

// ---------------------------------------------------------------------------
// Sélection au rayon
// ---------------------------------------------------------------------------
void EditorNode::_pick_recursive_3d(Node *p_node, const Vector3 &p_origin, const Vector3 &p_direction, real_t &r_best, Node *&r_found) const {
	Node3D *n = Object::cast_to<Node3D>(p_node);
	if (n) {
		if (!n->is_visible()) {
			return;
		}
		const Transform3D global = n->get_global_transform();
		real_t distance = -1;
		if (std::fabs(global.basis.determinant()) > 1e-9f) {
			// Rayon dans le repère du nœud : le paramètre t reste le même que dans le monde.
			const Transform3D inv = global.affine_inverse();
			const Vector3 from = inv.xform(p_origin);
			const Vector3 dir = inv.basis.xform(p_direction);
			MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_node);
			AABB box;
			real_t t = 0;
			if (mi && mi->get_mesh().is_valid()) {
				if (mi->get_mesh()->intersect_ray(from, dir, &t)) {
					distance = t;
				}
			} else if (n->_edit_get_aabb(box)) {
				if (box.grow(0.001f).intersects_ray(from, dir, &t)) {
					// Léger handicap : à distance égale, le maillage visible l'emporte sur la
					// forme de collision qui l'enveloppe.
					distance = t + 0.06f;
				}
			}
		}
		if (distance < 0 && !Object::cast_to<MeshInstance3D>(p_node)) {
			// Nœuds sans étendue (caméra, lumière, repère...) : petite sphère autour de l'origine.
			const real_t radius = std::max((real_t)0.2, (global.origin - p_origin).length() * 0.025f);
			const Vector3 to_center = global.origin - p_origin;
			const real_t along = to_center.dot(p_direction);
			if (along > 0 && (to_center - p_direction * along).length() <= radius) {
				distance = along - radius; // légère priorité sur ce qui se trouve derrière
			}
		}
		if (distance >= 0 && distance < r_best) {
			Node *target = p_node;
			while (target && !_is_editable(target)) {
				target = target->get_parent();
			}
			if (target) {
				r_best = distance;
				r_found = target;
			}
		}
	}
	for (Node *child : p_node->get_children_vector()) {
		_pick_recursive_3d(child, p_origin, p_direction, r_best, r_found);
	}
}

Node *EditorNode::_pick_node_3d(const Vector3 &p_origin, const Vector3 &p_direction) const {
	Node *found = nullptr;
	real_t best = 1e30f;
	if (scene_root) {
		_pick_recursive_3d(scene_root, p_origin, p_direction, best, found);
	}
	return found;
}

// ---------------------------------------------------------------------------
// Rendu de la scène éditée
// ---------------------------------------------------------------------------
void EditorNode::_collect_gizmo_lines_3d(Node *p_node) {
	if (Node3D *n = Object::cast_to<Node3D>(p_node)) {
		if (!n->is_visible()) {
			return;
		}
		n->_edit_get_gizmo_lines(&view3d_lines);
	}
	for (Node *child : p_node->get_children_vector()) {
		_collect_gizmo_lines_3d(child);
	}
}

void EditorNode::_render_edit_view_3d() {
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	if (!viewport_visible || !view3d_viewport.is_valid() || !edit_tree || !rs3d || !rs3d->is_available()) {
		return;
	}
	view3d_lines.clear();
	view3d_overlay_lines.clear();

	if (show_grid) {
		// Grille sur le plan XZ, centrée près de la cible ; le pas s'adapte au zoom.
		real_t step = 1.0f;
		while (step * 40.0f < cam3d_distance) {
			step *= 10.0f;
		}
		while (step * 4.0f > cam3d_distance && step > 0.011f) {
			step *= 0.1f;
		}
		const int half = 40;
		const real_t cx = std::round(cam3d_target.x / (step * 10.0f)) * step * 10.0f;
		const real_t cz = std::round(cam3d_target.z / (step * 10.0f)) * step * 10.0f;
		const real_t extent = step * (real_t)half;
		for (int i = -half; i <= half; i++) {
			const real_t x = cx + (real_t)i * step, z = cz + (real_t)i * step;
			const bool major = i % 10 == 0;
			const Color color = major ? Color(1, 1, 1, 0.22f) : Color(1, 1, 1, 0.09f);
			if (std::fabs(x) > step * 0.01f) {
				view3d_lines.push_back({ Vector3(x, 0, cz - extent), Vector3(x, 0, cz + extent), color });
			}
			if (std::fabs(z) > step * 0.01f) {
				view3d_lines.push_back({ Vector3(cx - extent, 0, z), Vector3(cx + extent, 0, z), color });
			}
		}
		// Axes du monde.
		view3d_lines.push_back({ Vector3(cx - extent, 0, 0), Vector3(cx + extent, 0, 0), Color(0.92f, 0.32f, 0.32f, 0.9f) });
		view3d_lines.push_back({ Vector3(0, 0, cz - extent), Vector3(0, 0, cz + extent), Color(0.32f, 0.55f, 0.96f, 0.9f) });
		view3d_lines.push_back({ Vector3(0, 0, 0), Vector3(0, extent * 0.1f, 0), Color(0.45f, 0.85f, 0.32f, 0.9f) });
	}
	if (scene_root) {
		_collect_gizmo_lines_3d(scene_root);
	}
	// Boîte de sélection.
	if (Node3D *selected = Object::cast_to<Node3D>(_get_selected())) {
		AABB box;
		if (selected->_edit_get_aabb(box)) {
			DebugLines3D::add_box(&view3d_overlay_lines, selected->get_global_transform(), box.grow(0.005f), Color(1.0f, 0.55f, 0.16f, 1));
		}
	}

	RenderingServer3D::RenderExtras extras;
	extras.lines = &view3d_lines;
	extras.overlay_lines = &view3d_overlay_lines;
	extras.fallback_background = palette.dark ? Color(0.16f, 0.17f, 0.2f, 1) : Color(0.62f, 0.64f, 0.68f, 1);
	rs3d->render(view3d_viewport, edit_tree->get_scenario(), _get_camera_3d(), extras);
}

// ---------------------------------------------------------------------------
// Dock « Vue » en mode 3D
// ---------------------------------------------------------------------------
void EditorNode::_draw_viewport_3d() {
	auto tool_button = [this](const char *p_label, Tool p_tool, const char *p_tip) {
		if (_toggle_button(p_label, tool == p_tool, p_tip)) {
			tool = p_tool;
		}
		ImGui::SameLine();
	};
	tool_button(ICON_TOOL_MOVE "  Déplacer", TOOL_SELECT, "Sélectionner et déplacer (Q)\nFaites glisser un axe, ou le centre pour un déplacement libre");
	tool_button(ICON_TOOL_ROTATE "  Pivoter", TOOL_ROTATE, "Faire pivoter autour d'un axe (E)\nCtrl : pas de 15°");
	tool_button(ICON_TOOL_SCALE "  Échelle", TOOL_SCALE, "Redimensionner selon un axe (R)\nCentre : échelle uniforme");
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine();
	if (_toggle_button(ICON_GRID "##grid", show_grid, "Afficher la grille")) {
		show_grid = !show_grid;
	}
	ImGui::SameLine();
	if (_toggle_button(ICON_NODE_3D "  Local", gizmo_local, "Axes du manipulateur alignés sur le nœud plutôt que sur le monde")) {
		gizmo_local = !gizmo_local;
	}
	ImGui::SameLine();
	if (_toggle_button(ICON_MAGNET "##snap_on", snap_enabled, "Aimanter les déplacements au pas indiqué")) {
		snap_enabled = !snap_enabled;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ES(64));
	if (ImGui::DragFloat("##snap3d", &snap_step_3d, 0.05f, 0.01f, 100.0f, "%.2f m")) {
		snap_step_3d = std::max(0.01f, snap_step_3d);
	}
	ImGui::SetItemTooltip("Pas de l'aimant");
	ImGui::SameLine();
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine();
	if (ImGui::Button(ICON_FOCUS "  Centrer")) {
		_focus_selection_3d();
	}
	ImGui::SetItemTooltip("Centrer la vue sur la sélection (F)");
	ImGui::SameLine();
	if (ImGui::Button(ICON_EYE "  Vue")) {
		ImGui::OpenPopup("##vue3d");
	}
	ImGui::SetItemTooltip("Projection et vues prédéfinies");
	if (ImGui::BeginPopup("##vue3d")) {
		if (ImGui::MenuItem("Perspective", "Pavé num. 5", !cam3d_orthogonal)) {
			cam3d_orthogonal = false;
		}
		if (ImGui::MenuItem("Orthogonale", "Pavé num. 5", cam3d_orthogonal)) {
			cam3d_orthogonal = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Face", "Pavé num. 1")) {
			cam3d_yaw = 0;
			cam3d_pitch = 0;
		}
		if (ImGui::MenuItem("Droite", "Pavé num. 3")) {
			cam3d_yaw = (real_t)Math_PI * 0.5f;
			cam3d_pitch = 0;
		}
		if (ImGui::MenuItem("Dessus", "Pavé num. 7")) {
			cam3d_yaw = 0;
			cam3d_pitch = -(real_t)Math_PI * 0.5f;
		}
		ImGui::EndPopup();
	}

	// --- Zone de dessin ---
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const Size2 size(std::max(16.0f, std::floor(avail.x)), std::max(16.0f, std::floor(avail.y)));
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	if (!rs3d || !rs3d->is_available()) {
		ImGui::Dummy(ImVec2(0, size.y * 0.4f));
		const char *message = "OpenGL n'est pas disponible : la vue 3D ne peut pas être affichée.";
		ImGui::SetCursorPosX(std::max(8.0f, (ImGui::GetWindowWidth() - ImGui::CalcTextSize(message).x) * 0.5f));
		ImGui::TextDisabled("%s", message);
		return;
	}
	// Rendu en pixels réels (écrans à haute densité) ; view3d_size reste en unités d'interface.
	const int pixels_w = std::max(1, (int)std::round(size.x * pixel_ratio)), pixels_h = std::max(1, (int)std::round(size.y * pixel_ratio));
	if (!view3d_viewport.is_valid()) {
		view3d_viewport = rs3d->viewport_create(pixels_w, pixels_h);
		view3d_size = size;
		_focus_selection_3d();
	} else if (view3d_size != size || rs3d->viewport_get_size(view3d_viewport) != Size2((real_t)pixels_w, (real_t)pixels_h)) {
		rs3d->viewport_set_size(view3d_viewport, pixels_w, pixels_h);
		view3d_size = size;
	}
	viewport_visible = true;

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##canvas3d", ImVec2(size.x, size.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	ImDrawList *draw_list = ImGui::GetWindowDrawList();
	draw_list->AddImage(_texture_id(rs3d->viewport_get_texture(view3d_viewport)), origin, ImVec2(origin.x + size.x, origin.y + size.y));

	// Dépôt d'un fichier : instancie une scène ou crée un MeshInstance3D (.obj, .glb, .gltf).
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MEX_FILE")) {
			const String path((const char *)payload->Data);
			const Vector2 mouse(ImGui::GetMousePos().x - origin.x, ImGui::GetMousePos().y - origin.y);
			Vector3 ray_origin, ray_dir, drop = cam3d_target;
			_get_mouse_ray_3d(mouse, size, ray_origin, ray_dir);
			_ray_plane(ray_origin, ray_dir, Vector3(), Vector3(0, 1, 0), drop); // posé sur le sol (Y = 0)
			_defer([this, path, drop]() {
				if (!scene_root) {
					print_warning("Créez d'abord un nœud racine avant de déposer un fichier dans la vue.");
					return;
				}
				const String ext = path_get_extension(path);
				Node *created = nullptr;
				if (ext == "scn") {
					created = _instance_scene(path, scene_root);
				} else if (ext == "obj" || ext == "glb" || ext == "gltf") {
					const String node_name = str_capitalize(path_get_basename(path_get_file(path)));
					Ref<ArrayMesh> array_mesh = ResourceLoader::load(path);
					GLTFData gltf;
					const bool animated = ext != "obj" && array_mesh.is_valid() && gltf_load(path, gltf) == OK && (gltf.has_skins || !gltf.clips.empty());
					if (animated) {
						// Modèle avec peau ou animations : un Skeleton3D, et le maillage en enfant.
						created = _create_node("Skeleton3D", scene_root);
						if (created) {
							created->set("source", Variant(path));
							created->set_name(node_name);
							if (Node *mesh_node = _create_node("MeshInstance3D", created)) {
								mesh_node->set("mesh", Variant(ResourceLoader::load(path)));
								mesh_node->set_name("Maillage");
							}
						}
					} else {
						created = _create_node("MeshInstance3D", scene_root);
						if (created) {
							created->set("mesh", Variant(ResourceLoader::load(path)));
							created->set_name(node_name);
						}
					}
				}
				if (Node3D *n = Object::cast_to<Node3D>(created)) {
					n->set_global_position(_round3(drop));
					_mark_modified();
				}
			});
		}
		ImGui::EndDragDropTarget();
	}

	_viewport_3d_input(Vector2(origin.x, origin.y), size, hovered);
	draw_list->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	_viewport_3d_overlay(Vector2(origin.x, origin.y), size);
	draw_list->PopClipRect();
}

// Axes du manipulateur pour le nœud sélectionné et longueur (monde) correspondant à
// GIZMO_PIXELS à l'écran.
static bool _gizmo_frame(const RenderingServer3D::CameraData &p_camera, const Size2 &p_size, const Node3D *p_node, bool p_local, Vector3 r_axes[3], real_t &r_length) {
	const Transform3D global = p_node->get_global_transform();
	const Basis basis = p_local ? global.basis.orthonormalized() : Basis();
	for (int i = 0; i < 3; i++) {
		r_axes[i] = basis.get_column(i);
	}
	real_t world_per_pixel;
	if (p_camera.orthogonal) {
		world_per_pixel = p_camera.size / std::max(p_size.y, 1.0f);
	} else {
		const real_t depth = (global.origin - p_camera.transform.origin).dot(-p_camera.transform.basis.get_column(2));
		if (depth <= p_camera.z_near) {
			return false;
		}
		world_per_pixel = 2.0f * std::tan(Math::deg_to_rad(p_camera.fov) * 0.5f) * depth / std::max(p_size.y, 1.0f);
	}
	r_length = world_per_pixel * GIZMO_PIXELS;
	return true;
}

void EditorNode::_viewport_3d_input(const Vector2 &p_origin, const Size2 &p_size, bool p_hovered) {
	ImGuiIO &io = ImGui::GetIO();
	const Vector2 mouse(io.MousePos.x - p_origin.x, io.MousePos.y - p_origin.y);
	const bool window_focused_now = ImGui::IsWindowFocused();
	const RenderingServer3D::CameraData camera = _get_camera_3d();

	// Zoom.
	if (p_hovered && io.MouseWheel != 0.0f) {
		cam3d_distance = Math::clamp(cam3d_distance * std::pow(0.88f, io.MouseWheel), (real_t)0.05, (real_t)5000.0);
	}

	// Raccourcis clavier.
	if ((p_hovered || window_focused_now) && !io.WantTextInput && !io.KeyCtrl && drag3d == DRAG3D_NONE) {
		if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
			tool = TOOL_SELECT;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
			tool = TOOL_ROTATE;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			tool = TOOL_SCALE;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			_focus_selection_3d();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			_select(nullptr);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) {
			cam3d_yaw = 0;
			cam3d_pitch = 0;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) {
			cam3d_yaw = (real_t)Math_PI * 0.5f;
			cam3d_pitch = 0;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) {
			cam3d_yaw = 0;
			cam3d_pitch = -(real_t)Math_PI * 0.5f;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Keypad5, false)) {
			cam3d_orthogonal = !cam3d_orthogonal;
		}
		Node *selected = _get_selected();
		if (selected && window_focused_now && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
			_defer([this, selected]() { _delete_node(selected); });
		}
	}

	// --- Caméra : orbite (bouton droit ou du milieu), panoramique (Maj) ---
	if (drag3d == DRAG3D_NONE && p_hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
		drag3d = io.KeyShift ? DRAG3D_PAN : DRAG3D_ORBIT;
	}
	if (drag3d == DRAG3D_ORBIT || drag3d == DRAG3D_PAN) {
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
			drag3d = DRAG3D_NONE;
		} else if (drag3d == DRAG3D_ORBIT) {
			cam3d_yaw -= io.MouseDelta.x * 0.008f;
			cam3d_pitch = Math::clamp(cam3d_pitch - io.MouseDelta.y * 0.008f, -(real_t)Math_PI * 0.5f, (real_t)Math_PI * 0.5f);
		} else {
			const real_t world_per_pixel = (camera.orthogonal ? camera.size : 2.0f * std::tan(Math::deg_to_rad(camera.fov) * 0.5f) * cam3d_distance) / std::max(p_size.y, 1.0f);
			cam3d_target += (camera.transform.basis.get_column(1) * io.MouseDelta.y - camera.transform.basis.get_column(0) * io.MouseDelta.x) * world_per_pixel;
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		}
		return;
	}

	Vector3 ray_origin, ray_dir;
	_get_mouse_ray_3d(mouse, p_size, ray_origin, ray_dir);

	Node3D *node = Object::cast_to<Node3D>(_get_selected());
	Vector3 axes[3];
	real_t length = 1;
	const bool has_gizmo = node && _gizmo_frame(camera, p_size, node, gizmo_local || tool == TOOL_SCALE, axes, length);
	const Vector3 pivot = node ? node->get_global_transform().origin : Vector3();

	// --- Manipulateur : survol ---
	if (drag3d == DRAG3D_NONE) {
		gizmo_hover = -1;
		Vector2 center;
		if (has_gizmo && p_hovered && _project_3d(pivot, p_size, center)) {
			float best = GIZMO_PICK_PIXELS;
			if ((mouse - center).length() < 9.0f && tool != TOOL_ROTATE) {
				gizmo_hover = 3;
				best = 0;
			}
			for (int i = 0; i < 3 && gizmo_hover != 3; i++) {
				float d = 1e9f;
				if (tool == TOOL_ROTATE) {
					// Anneau perpendiculaire à l'axe.
					const Vector3 u = axes[(i + 1) % 3], v = axes[(i + 2) % 3];
					Vector2 previous;
					bool has_previous = false;
					for (int s = 0; s <= 48; s++) {
						const real_t a = (real_t)s / 48.0f * (real_t)Math_TAU;
						Vector2 p;
						if (!_project_3d(pivot + (u * std::cos(a) + v * std::sin(a)) * length, p_size, p)) {
							has_previous = false;
							continue;
						}
						if (has_previous) {
							d = std::min(d, _distance_to_segment(mouse, previous, p));
						}
						previous = p;
						has_previous = true;
					}
				} else {
					Vector2 tip;
					if (_project_3d(pivot + axes[i] * length, p_size, tip)) {
						d = _distance_to_segment(mouse, center, tip);
					}
				}
				if (d < best) {
					best = d;
					gizmo_hover = i;
				}
			}
		}

		if (p_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (gizmo_hover >= 0 && node && _is_editable(node)) {
				// Début d'une manipulation.
				drag3d = DRAG3D_GIZMO;
				gizmo_axis = gizmo_hover;
				drag3d_start_global = node->get_global_transform();
				drag3d_start_scale = node->get_scale();
				drag3d_start_mouse = mouse;
				drag3d_axis = gizmo_axis < 3 ? axes[gizmo_axis] : -camera.transform.basis.get_column(2);
				drag3d_start_param = 0;
				drag3d_start_point = pivot;
				if (tool == TOOL_ROTATE || gizmo_axis == 3) {
					_ray_plane(ray_origin, ray_dir, pivot, drag3d_axis, drag3d_start_point);
				} else {
					_closest_on_axis(pivot, drag3d_axis, ray_origin, ray_dir, drag3d_start_param);
				}
			} else {
				_select(_pick_node_3d(ray_origin, ray_dir));
			}
		}
		return;
	}

	// --- Manipulation en cours ---
	if (drag3d != DRAG3D_GIZMO) {
		return;
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !node) {
		drag3d = DRAG3D_NONE;
		gizmo_axis = -1;
		return;
	}
	const Vector3 start_origin = drag3d_start_global.origin;
	Node3D *parent = Object::cast_to<Node3D>(node->get_parent());
	const Transform3D parent_global = parent ? parent->get_global_transform() : Transform3D();

	switch (tool) {
		case TOOL_SELECT: {
			Vector3 delta;
			if (gizmo_axis == 3) {
				Vector3 hit;
				if (!_ray_plane(ray_origin, ray_dir, start_origin, drag3d_axis, hit)) {
					break;
				}
				delta = hit - drag3d_start_point;
				if (snap_enabled) {
					delta = Vector3(std::round(delta.x / snap_step_3d), std::round(delta.y / snap_step_3d), std::round(delta.z / snap_step_3d)) * snap_step_3d;
				}
			} else {
				real_t t = 0;
				if (!_closest_on_axis(start_origin, drag3d_axis, ray_origin, ray_dir, t)) {
					break;
				}
				real_t amount = t - drag3d_start_param;
				if (snap_enabled) {
					amount = std::round(amount / snap_step_3d) * snap_step_3d;
				}
				delta = drag3d_axis * amount;
			}
			if (std::fabs(parent_global.basis.determinant()) < 1e-9f) {
				break;
			}
			const Vector3 new_position = _round3(parent_global.affine_inverse().xform(start_origin + delta));
			if (new_position != node->get_position()) {
				node->set_position(new_position);
				_mark_modified();
			}
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		} break;
		case TOOL_ROTATE: {
			real_t angle = 0;
			Vector3 hit;
			const Vector3 from = drag3d_start_point - start_origin;
			// Plan de rotation vu presque par la tranche : on se rabat sur le déplacement horizontal.
			if (std::fabs(ray_dir.dot(drag3d_axis)) > 0.12f && from.length() > 1e-5f && _ray_plane(ray_origin, ray_dir, start_origin, drag3d_axis, hit)) {
				const Vector3 to = hit - start_origin;
				angle = std::atan2(drag3d_axis.dot(from.cross(to)), from.dot(to));
			} else {
				angle = (mouse.x - drag3d_start_mouse.x) * 0.012f;
			}
			if (io.KeyCtrl) {
				const real_t step = (real_t)Math::deg_to_rad(15.0);
				angle = std::round(angle / step) * step;
			}
			drag3d_start_param = angle; // affiché par la surcouche
			Transform3D global = drag3d_start_global;
			global.basis = Basis(drag3d_axis, angle) * drag3d_start_global.basis;
			if (global != node->get_global_transform()) {
				node->set_global_transform(global);
				node->set_scale(drag3d_start_scale); // la rotation ne doit pas altérer l'échelle
				_mark_modified();
			}
		} break;
		case TOOL_SCALE: {
			Vector3 new_scale = drag3d_start_scale;
			if (gizmo_axis == 3) {
				const real_t factor = std::max((real_t)0.01, 1.0f + (drag3d_start_mouse.y - mouse.y) * 0.01f);
				new_scale = drag3d_start_scale * factor;
			} else {
				real_t t = 0;
				if (!_closest_on_axis(start_origin, drag3d_axis, ray_origin, ray_dir, t) || std::fabs(drag3d_start_param) < 1e-5f) {
					break;
				}
				const real_t factor = std::max((real_t)0.01, t / drag3d_start_param);
				new_scale[gizmo_axis] = drag3d_start_scale[gizmo_axis] * factor;
			}
			new_scale = _round3(new_scale);
			if (new_scale != node->get_scale()) {
				node->set_scale(new_scale);
				_mark_modified();
			}
		} break;
	}
}

void EditorNode::_viewport_3d_overlay(const Vector2 &p_origin, const Size2 &p_size) {
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const RenderingServer3D::CameraData camera = _get_camera_3d();
	auto to_im = [&](const Vector2 &p) { return ImVec2(p_origin.x + p.x, p_origin.y + p.y); };

	Node3D *node = Object::cast_to<Node3D>(_get_selected());
	Vector3 axes[3];
	real_t length = 1;
	Vector2 center;
	if (node && _gizmo_frame(camera, p_size, node, gizmo_local || tool == TOOL_SCALE, axes, length) && _project_3d(node->get_global_transform().origin, p_size, center)) {
		const Vector3 pivot = node->get_global_transform().origin;
		const int active = drag3d == DRAG3D_GIZMO ? gizmo_axis : gizmo_hover;
		for (int i = 0; i < 3; i++) {
			const ImU32 color = active == i ? AXIS_HIGHLIGHT : AXIS_COLORS[i];
			const float thickness = active == i ? 3.0f : 2.0f;
			if (tool == TOOL_ROTATE) {
				const Vector3 u = axes[(i + 1) % 3], v = axes[(i + 2) % 3];
				Vector2 previous;
				bool has_previous = false;
				for (int s = 0; s <= 48; s++) {
					const real_t a = (real_t)s / 48.0f * (real_t)Math_TAU;
					const Vector3 world = pivot + (u * std::cos(a) + v * std::sin(a)) * length;
					Vector2 p;
					if (!_project_3d(world, p_size, p)) {
						has_previous = false;
						continue;
					}
					if (has_previous) {
						// La moitié arrière de l'anneau est atténuée.
						const bool back = (world - pivot).dot(camera.transform.basis.get_column(2)) < 0 && !camera.orthogonal;
						dl->AddLine(to_im(previous), to_im(p), back ? (color & 0x00FFFFFF) | 0x50000000 : color, thickness);
					}
					previous = p;
					has_previous = true;
				}
				continue;
			}
			Vector2 tip;
			if (!_project_3d(pivot + axes[i] * length, p_size, tip)) {
				continue;
			}
			dl->AddLine(to_im(center), to_im(tip), color, thickness);
			const Vector2 dir = (tip - center).length() > 1e-3f ? (tip - center).normalized() : Vector2(1, 0);
			const Vector2 side(-dir.y, dir.x);
			if (tool == TOOL_SCALE) {
				dl->AddRectFilled(ImVec2(p_origin.x + tip.x - 5, p_origin.y + tip.y - 5), ImVec2(p_origin.x + tip.x + 5, p_origin.y + tip.y + 5), color);
			} else {
				dl->AddTriangleFilled(to_im(tip + dir * 12.0f), to_im(tip + side * 5.5f), to_im(tip - side * 5.5f), color);
			}
		}
		if (tool != TOOL_ROTATE) {
			const ImU32 color = active == 3 ? AXIS_HIGHLIGHT : IM_COL32(235, 235, 235, 255);
			if (tool == TOOL_SCALE) {
				dl->AddRectFilled(ImVec2(p_origin.x + center.x - 5, p_origin.y + center.y - 5), ImVec2(p_origin.x + center.x + 5, p_origin.y + center.y + 5), color);
			} else {
				dl->AddCircleFilled(to_im(center), 5.0f, color);
			}
		} else {
			dl->AddCircleFilled(to_im(center), 3.0f, IM_COL32(235, 235, 235, 255));
		}

		if (drag3d == DRAG3D_GIZMO) {
			String text;
			if (tool == TOOL_SELECT) {
				const Vector3 p = node->get_position();
				text = vformat("(%.2f, %.2f, %.2f)", (double)p.x, (double)p.y, (double)p.z);
			} else if (tool == TOOL_ROTATE) {
				text = vformat("%.1f°", (double)Math::rad_to_deg(drag3d_start_param));
			} else {
				const Vector3 s = node->get_scale();
				text = vformat("(%.2f, %.2f, %.2f)", (double)s.x, (double)s.y, (double)s.z);
			}
			dl->AddText(ImVec2(p_origin.x + center.x + 14, p_origin.y + center.y - 24), IM_COL32_WHITE, text.c_str());
		}
	}

	// Repère d'orientation (coin supérieur droit).
	{
		const ImVec2 c(p_origin.x + p_size.x - 44, p_origin.y + 44);
		dl->AddCircleFilled(c, 34.0f, IM_COL32(20, 22, 26, 150));
		const Basis inv = camera.transform.basis.transposed();
		struct AxisDraw {
			float depth;
			ImVec2 tip;
			ImU32 color;
			const char *label;
		};
		const char *labels[3] = { "X", "Y", "Z" };
		AxisDraw order[3];
		for (int i = 0; i < 3; i++) {
			Vector3 axis;
			axis[i] = 1;
			const Vector3 v = inv.xform(axis);
			order[i] = { v.z, ImVec2(c.x + v.x * 26.0f, c.y - v.y * 26.0f), AXIS_COLORS[i], labels[i] };
		}
		std::sort(order, order + 3, [](const AxisDraw &a, const AxisDraw &b) { return a.depth < b.depth; });
		for (const AxisDraw &a : order) {
			dl->AddLine(c, a.tip, a.color, 2.0f);
			dl->AddCircleFilled(a.tip, 7.0f, a.color);
			const ImVec2 ts = ImGui::CalcTextSize(a.label);
			dl->AddText(ImVec2(a.tip.x - ts.x * 0.5f, a.tip.y - ts.y * 0.5f), IM_COL32(20, 20, 20, 255), a.label);
		}
	}

	// Légende.
	const String status = String(cam3d_orthogonal ? "Orthogonale" : "Perspective") + "   Clic droit : orbite   Maj + clic droit : déplacer   Molette : zoom";
	dl->AddText(ImVec2(p_origin.x + 8, p_origin.y + p_size.y - 22), IM_COL32(200, 205, 215, 170), status.c_str());

	if (!scene_root) {
		const char *hint = "Scène vide : ajoutez un nœud racine depuis le dock « Scène ».";
		const ImVec2 text_size = ImGui::CalcTextSize(hint);
		dl->AddText(ImVec2(p_origin.x + (p_size.x - text_size.x) * 0.5f, p_origin.y + p_size.y * 0.5f), IM_COL32(200, 205, 215, 200), hint);
	}
}
