// Aperçus : miniatures des fichiers du projet (images, maillages 3D, scènes, sons, textes)
// et aperçus 3D interactifs des ressources dans l'inspecteur (maillages, matériaux).
//
// Les aperçus sont générés à la demande, un petit nombre par image, hors du parcours de
// l'interface : _get_preview() met le fichier en file d'attente, _render_previews() le traite.
#include "editor_node.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/string/ustring.h"
#include "editor_ui.h"
#include "scene/3d/visual_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/main/canvas_item.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/packed_scene.h"
#include "servers/audio_server.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {

using EditorUI::col;
using EditorUI::u32;

const int PREVIEW_PIXELS = 256; // côté des miniatures rendues
const int MAX_RENDERED_PREVIEWS = 96; // au-delà, les fichiers gardent leur icône
const float PREVIEW_FOV = 40.0f;

ImTextureID _tex(RID p_texture) {
	return (ImTextureID)(intptr_t)RenderingServer::get_singleton()->texture_get_native(p_texture);
}

// 1204 -> « 1 204 »
String _thousands(int p_value) {
	String digits = itos(p_value);
	String out;
	for (size_t i = 0; i < digits.size(); i++) {
		if (i > 0 && (digits.size() - i) % 3 == 0) {
			out += " ";
		}
		out += digits[i];
	}
	return out;
}

String _file_size(const String &p_path) {
	const std::vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path);
	const double size = (double)bytes.size();
	if (size < 1024.0) {
		return vformat("%d octets", (int)size);
	}
	if (size < 1024.0 * 1024.0) {
		return vformat("%.1f Ko", size / 1024.0);
	}
	return vformat("%.1f Mo", size / (1024.0 * 1024.0));
}

bool _has_light(Node *p_node) {
	if (Object::cast_to<Light3D>(p_node)) {
		return true;
	}
	for (Node *child : p_node->get_children_vector()) {
		if (_has_light(child)) {
			return true;
		}
	}
	return false;
}

// Damier discret derrière les images qui ont de la transparence.
void _checkerboard(ImDrawList *p_dl, const ImVec2 &p_min, const ImVec2 &p_max, bool p_dark) {
	const float cell = std::max(4.0f, ES(8));
	const ImU32 a = p_dark ? IM_COL32(52, 54, 62, 255) : IM_COL32(236, 236, 240, 255);
	const ImU32 b = p_dark ? IM_COL32(40, 42, 50, 255) : IM_COL32(210, 210, 216, 255);
	p_dl->AddRectFilled(p_min, p_max, a);
	p_dl->PushClipRect(p_min, p_max, true);
	int row = 0;
	for (float y = p_min.y; y < p_max.y; y += cell, row++) {
		for (float x = p_min.x + ((row & 1) ? cell : 0.0f); x < p_max.x; x += cell * 2.0f) {
			p_dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell, y + cell), b);
		}
	}
	p_dl->PopClipRect();
}

} // namespace

// ---------------------------------------------------------------------------
// Fichiers
// ---------------------------------------------------------------------------
int EditorNode::_get_preview_kind(const String &p_path) {
	const String ext = path_get_extension(p_path);
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga" || ext == "gif") {
		return PREVIEW_IMAGE;
	}
	if (ext == "obj") {
		return PREVIEW_MESH;
	}
	if (ext == "scn") {
		return PREVIEW_SCENE;
	}
	if (ext == "wav" || ext == "ogg" || ext == "mp3") {
		return PREVIEW_AUDIO;
	}
	if (ext == "mx" || ext == "txt" || ext == "md" || ext == "json" || ext == "cfg" || ext == "mex" || ext == "res") {
		return PREVIEW_TEXT;
	}
	return PREVIEW_NONE;
}

EditorNode::FilePreview *EditorNode::_get_preview(const String &p_path) {
	if (!pref_thumbnails) {
		return nullptr;
	}
	const int kind = _get_preview_kind(p_path);
	if (kind == PREVIEW_NONE) {
		return nullptr;
	}
	FilePreview &preview = previews[p_path];
	preview.kind = kind;
	if (!preview.ready && !preview.queued) {
		preview.queued = true;
		preview_queue.push_back(p_path);
	}
	return &preview;
}

bool EditorNode::_has_preview_image(const FilePreview *p_preview) const {
	return p_preview && p_preview->ready && (p_preview->texture.is_valid() || p_preview->viewport.is_valid() || p_preview->target.is_valid());
}

void EditorNode::_free_preview(FilePreview &r_preview) {
	if (r_preview.viewport.is_valid() && RenderingServer3D::get_singleton()) {
		RenderingServer3D::get_singleton()->free_rid(r_preview.viewport);
	}
	if (r_preview.target.is_valid()) {
		RenderingServer::get_singleton()->free_rid(r_preview.target);
	}
	r_preview = FilePreview();
}

void EditorNode::_clear_previews() {
	for (auto &entry : previews) {
		_free_preview(entry.second);
	}
	previews.clear();
	preview_queue.clear();
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	for (auto &entry : live_previews) {
		if (rs3d && entry.second.viewport.is_valid()) {
			rs3d->free_rid(entry.second.viewport);
		}
	}
	live_previews.clear();
	preview_sphere.unref();
	if (rs3d) {
		for (RID *rid : { &preview_instance, &preview_key_light, &preview_fill_light, &preview_scenario }) {
			if (rid->is_valid()) {
				rs3d->free_rid(*rid);
				*rid = RID();
			}
		}
	}
}

// Rend un maillage seul, cadré sur sa boîte englobante, dans p_viewport.
bool EditorNode::_render_preview_3d(RID p_viewport, RID p_mesh, const AABB &p_aabb, RID p_material, float p_yaw, float p_pitch) {
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	if (!rs3d || !rs3d->is_available() || !p_viewport.is_valid() || !p_mesh.is_valid()) {
		return false;
	}
	if (!preview_scenario.is_valid()) {
		preview_scenario = rs3d->scenario_create();
		preview_instance = rs3d->instance_create(preview_scenario);
		rs3d->instance_set_cast_shadows(preview_instance, false);
		// Lumière principale en haut à gauche, lumière d'appoint froide à l'opposé.
		RenderingServer3D::LightData key;
		key.energy = 1.25f;
		key.color = Color(1.0f, 0.97f, 0.92f, 1.0f);
		preview_key_light = rs3d->light_create(preview_scenario);
		rs3d->light_set_data(preview_key_light, key);
		RenderingServer3D::LightData fill;
		fill.energy = 0.35f;
		fill.color = Color(0.75f, 0.85f, 1.0f, 1.0f);
		preview_fill_light = rs3d->light_create(preview_scenario);
		rs3d->light_set_data(preview_fill_light, fill);
	}
	const Vector3 center = p_aabb.get_center();
	const real_t radius = std::max((real_t)0.01, p_aabb.size.length() * 0.5f);
	RenderingServer3D::CameraData camera;
	camera.fov = PREVIEW_FOV;
	camera.transform.basis = Basis::from_euler(Vector3(p_pitch, p_yaw, 0));
	const real_t distance = radius / std::sin(Math::deg_to_rad(PREVIEW_FOV) * 0.5f) * 1.04f;
	camera.transform.origin = center + camera.transform.basis.get_column(2) * distance;
	camera.z_near = std::max((real_t)0.001, distance - radius * 1.5f);
	camera.z_far = distance + radius * 3.0f;
	// Les lumières suivent la caméra : l'éclairage reste le même quand l'objet tourne.
	rs3d->light_set_transform(preview_key_light, Transform3D(camera.transform.basis * Basis::from_euler(Vector3(-0.75f, 0.65f, 0)), Vector3()));
	rs3d->light_set_transform(preview_fill_light, Transform3D(camera.transform.basis * Basis::from_euler(Vector3(0.35f, -2.2f, 0)), Vector3()));
	rs3d->instance_set_mesh(preview_instance, p_mesh);
	rs3d->instance_set_material(preview_instance, p_material);
	rs3d->instance_set_transform(preview_instance, Transform3D());
	RenderingServer3D::RenderExtras extras;
	extras.use_environment = false;
	extras.fallback_background = palette.dark ? Color(0.115f, 0.12f, 0.15f, 1.0f) : Color(0.80f, 0.81f, 0.85f, 1.0f);
	rs3d->render(p_viewport, preview_scenario, camera, extras);
	return true;
}

// Miniature d'une scène : elle est instanciée dans un arbre temporaire, sans exécuter ses
// scripts, puis rendue une fois (3D : cadrée sur sa boîte englobante ; 2D : sur son étendue).
void EditorNode::_render_scene_preview(const String &p_path, FilePreview &r_preview, int p_size) {
	Ref<PackedScene> packed = ResourceLoader::load(p_path, nullptr, true);
	if (packed.is_null()) {
		return;
	}
	Node *root = nullptr;
	{
		ScriptingDisabledScope no_scripts;
		root = packed->instantiate();
	}
	if (!root) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	const Size2 game_frame((real_t)GLOBAL_GET("display/width").as_int(), (real_t)GLOBAL_GET("display/height").as_int());
	SceneTree *tree = memnew(SceneTree(true));
	tree->set_viewport_size(game_frame);
	{
		ScriptingDisabledScope no_scripts;
		tree->get_root()->add_child(root);
	}
	int nodes = 0;
	std::vector<Node *> stack = { root };
	while (!stack.empty()) {
		Node *node = stack.back();
		stack.pop_back();
		nodes++;
		for (Node *child : node->get_children_vector()) {
			stack.push_back(child);
		}
	}
	r_preview.info = vformat("%s  ·  %d nœud%s", root->get_class().c_str(), nodes, nodes > 1 ? "s" : "");

	AABB box;
	const bool is_3d = !Object::cast_to<CanvasItem>(root) && _get_world_aabb(root, box);
	if (is_3d) {
		if (rs3d && rs3d->is_available()) {
			const Vector3 center = box.get_center();
			const real_t radius = std::max((real_t)0.05, box.size.length() * 0.5f);
			RenderingServer3D::CameraData camera;
			camera.fov = PREVIEW_FOV;
			camera.transform.basis = Basis::from_euler(Vector3(-0.42f, 0.62f, 0));
			const real_t distance = radius / std::sin(Math::deg_to_rad(PREVIEW_FOV) * 0.5f) * 0.92f;
			camera.transform.origin = center + camera.transform.basis.get_column(2) * distance;
			camera.z_near = std::max((real_t)0.01, distance * 0.01f);
			camera.z_far = distance + radius * 4.0f;
			// Scène sans lumière : une lumière temporaire, le temps du rendu.
			RID light;
			if (!_has_light(root)) {
				light = rs3d->light_create(tree->get_scenario());
				RenderingServer3D::LightData data;
				data.energy = 1.2f;
				rs3d->light_set_data(light, data);
				rs3d->light_set_transform(light, Transform3D(camera.transform.basis * Basis::from_euler(Vector3(-0.75f, 0.65f, 0)), Vector3()));
			}
			r_preview.viewport = rs3d->viewport_create(p_size, p_size);
			RenderingServer3D::RenderExtras extras;
			extras.fallback_background = palette.dark ? Color(0.115f, 0.12f, 0.15f, 1.0f) : Color(0.80f, 0.81f, 0.85f, 1.0f);
			rs3d->render(r_preview.viewport, tree->get_scenario(), camera, extras);
			if (light.is_valid()) {
				rs3d->free_rid(light);
			}
			r_preview.size = Size2((real_t)p_size, (real_t)p_size);
		}
	} else {
		Rect2 rect;
		if (!_get_canvas_bounds(root, rect)) {
			rect = Rect2(Vector2(), game_frame);
		}
		rect = rect.grow(std::max(rect.size.x, rect.size.y) * 0.04f);
		if (rect.size.x > 0.5f && rect.size.y > 0.5f) {
			// La miniature garde les proportions de la scène, dans un carré de p_size pixels.
			const real_t scale = (real_t)p_size / std::max(rect.size.x, rect.size.y);
			const int width = std::max(8, (int)std::round(rect.size.x * scale));
			const int height = std::max(8, (int)std::round(rect.size.y * scale));
			r_preview.target = rs->texture_create_target(width, height);
			r_preview.size = Size2((real_t)width, (real_t)height);
			rs->set_render_target(r_preview.target);
			rs->clear(GLOBAL_GET("rendering/clear_color").as_color());
			const Transform2D view(0, Size2(scale, scale), -rect.position * scale);
			tree->draw(&view);
			rs->set_render_target(RID());
		}
	}
	memdelete(root);
	memdelete(tree);
}

void EditorNode::_generate_preview(const String &p_path, FilePreview &r_preview) {
	r_preview.ready = true;
	r_preview.queued = false;
	r_preview.mtime = FileAccess::get_modified_time(p_path);
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	int rendered = 0;
	for (const auto &entry : previews) {
		rendered += (entry.second.viewport.is_valid() || entry.second.target.is_valid()) ? 1 : 0;
	}
	const bool can_render = rendered < MAX_RENDERED_PREVIEWS;

	switch (r_preview.kind) {
		case PREVIEW_IMAGE: {
			Ref<Texture2D> texture = ResourceLoader::load(p_path);
			if (texture.is_valid() && texture->get_width() > 0) {
				r_preview.texture = texture;
				r_preview.size = texture->get_size();
				r_preview.info = vformat("%d x %d px  ·  %s", texture->get_width(), texture->get_height(), _file_size(p_path).c_str());
			}
		} break;
		case PREVIEW_MESH: {
			Ref<Mesh> mesh = ResourceLoader::load(p_path);
			if (mesh.is_valid()) {
				r_preview.info = vformat("%s triangles  ·  %s sommets", _thousands(mesh->get_triangle_count()).c_str(), _thousands(mesh->get_vertex_count()).c_str());
				if (can_render && rs3d && rs3d->is_available()) {
					r_preview.viewport = rs3d->viewport_create(PREVIEW_PIXELS, PREVIEW_PIXELS);
					r_preview.size = Size2(PREVIEW_PIXELS, PREVIEW_PIXELS);
					if (!_render_preview_3d(r_preview.viewport, mesh->get_rid(), mesh->get_aabb(), RID(), 0.62f, -0.36f)) {
						rs3d->free_rid(r_preview.viewport);
						r_preview.viewport = RID();
					}
				}
			}
		} break;
		case PREVIEW_SCENE: {
			if (can_render) {
				_render_scene_preview(p_path, r_preview, PREVIEW_PIXELS);
			}
		} break;
		case PREVIEW_AUDIO: {
			Ref<AudioStream> stream = ResourceLoader::load(p_path);
			if (stream.is_valid()) {
				const AudioServer::SampleData samples = stream->get_samples();
				const size_t frames = samples ? samples->size() / 2 : 0;
				const int columns = 96;
				r_preview.peaks.assign(columns, 0.0f);
				for (int column = 0; column < columns && frames > 0; column++) {
					const size_t begin = frames * (size_t)column / columns, end = std::max(begin + 1, frames * (size_t)(column + 1) / columns);
					// Pas plus de 256 échantillons lus par colonne : suffisant pour une silhouette.
					const size_t step = std::max<size_t>(1, (end - begin) / 256);
					float peak = 0.0f;
					for (size_t i = begin; i < end && i < frames; i += step) {
						peak = std::max(peak, std::max(std::fabs((*samples)[i * 2]), std::fabs((*samples)[i * 2 + 1])));
					}
					r_preview.peaks[column] = std::min(peak, 1.0f);
				}
				r_preview.info = vformat("%.2f s  ·  %s", stream->get_length(), _file_size(p_path).c_str());
			}
		} break;
		case PREVIEW_TEXT: {
			const String text = FileAccess::get_file_as_string(p_path);
			size_t start = 0;
			int total = text.empty() ? 0 : 1;
			for (char c : text) {
				total += c == '\n' ? 1 : 0;
			}
			while (start < text.size() && r_preview.lines.size() < 14) {
				size_t end = text.find('\n', start);
				if (end == String::npos) {
					end = text.size();
				}
				String line = text.substr(start, std::min<size_t>(end - start, 96));
				while (!line.empty() && ((unsigned char)line.back() & 0xC0) == 0x80) {
					line.pop_back(); // pas de caractère UTF-8 coupé en deux
				}
				if (!line.empty() && ((unsigned char)line.back() & 0x80)) {
					line.pop_back();
				}
				r_preview.lines.push_back(str_replace(line, "\t", "    "));
				start = end + 1;
			}
			r_preview.info = vformat("%d ligne%s  ·  %s", total, total > 1 ? "s" : "", _file_size(p_path).c_str());
		} break;
		default:
			break;
	}
}

// Appelé une fois par image, hors du parcours de l'interface.
void EditorNode::_render_previews() {
	preview_frame++;
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();

	// --- Miniatures en attente : deux par image, l'éditeur reste fluide ---
	for (int budget = 2; budget > 0 && !preview_queue.empty(); budget--) {
		const String path = preview_queue.front();
		preview_queue.erase(preview_queue.begin());
		auto it = previews.find(path);
		if (it != previews.end() && !it->second.ready) {
			_generate_preview(path, it->second);
		}
	}

	// --- Aperçus interactifs de l'inspecteur : rendus à chaque image tant qu'ils sont affichés ---
	for (auto it = live_previews.begin(); it != live_previews.end();) {
		LivePreview &live = it->second;
		if (live.last_used + 2 < preview_frame || !rs3d || !rs3d->is_available()) {
			if (rs3d && live.viewport.is_valid()) {
				rs3d->free_rid(live.viewport);
			}
			it = live_previews.erase(it);
			continue;
		}
		Ref<Mesh> mesh = Ref<Mesh>(Object::cast_to<Mesh>(live.mesh.ptr()));
		Ref<Material> material = Ref<Material>(Object::cast_to<Material>(live.material.ptr()));
		if (mesh.is_null()) {
			// Un matériau se présente sur une sphère.
			if (preview_sphere.is_null()) {
				Ref<SphereMesh> sphere;
				sphere.instantiate();
				preview_sphere = sphere;
			}
			mesh = Ref<Mesh>(Object::cast_to<Mesh>(preview_sphere.ptr()));
		}
		if (mesh.is_valid() && live.size > 0) {
			if (!live.viewport.is_valid()) {
				live.viewport = rs3d->viewport_create(live.size, live.size);
			} else if ((int)rs3d->viewport_get_size(live.viewport).x != live.size) {
				rs3d->viewport_set_size(live.viewport, live.size, live.size);
			}
			AABB box = mesh->get_aabb();
			if (live.mesh.is_null()) {
				// Sphère de présentation d'un matériau : cadrée au plus près (sa boîte englobante
				// la ferait paraître petite).
				box = AABB(box.get_center() - box.size * 0.31f, box.size * 0.62f);
			}
			_render_preview_3d(live.viewport, mesh->get_rid(), box, material.is_valid() ? material->get_rid() : RID(), live.yaw, live.pitch);
		}
		++it;
	}
	RenderingServer::get_singleton()->set_render_target(RID());
}

// ---------------------------------------------------------------------------
// Dessin
// ---------------------------------------------------------------------------
bool EditorNode::_draw_preview(const String &p_path, const Vector2 &p_min, const Vector2 &p_max) {
	FilePreview *preview = _get_preview(p_path);
	if (!preview || !preview->ready) {
		return false;
	}
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 box_min(p_min.x, p_min.y), box_max(p_max.x, p_max.y);
	const float box_w = box_max.x - box_min.x, box_h = box_max.y - box_min.y;
	const float rounding = std::min(ES(6), box_w * 0.12f);

	if (_has_preview_image(preview)) {
		// Image centrée, proportions conservées ; une petite image (pixel art) n'est agrandie
		// que d'un facteur entier pour rester nette.
		const Size2 size = preview->size;
		float scale = std::min(box_w / size.x, box_h / size.y);
		if (preview->texture.is_valid() && scale > 1.0f) {
			scale = std::min(std::floor(scale), 8.0f);
		}
		const float w = std::max(1.0f, std::floor(size.x * scale)), h = std::max(1.0f, std::floor(size.y * scale));
		const ImVec2 pos(std::floor(box_min.x + (box_w - w) * 0.5f), std::floor(box_min.y + (box_h - h) * 0.5f));
		const ImVec2 end(pos.x + w, pos.y + h);
		if (preview->texture.is_valid()) {
			_checkerboard(dl, pos, end, palette.dark);
			dl->AddImage(_tex(preview->texture->get_rid()), pos, end);
		} else {
			const RID texture = preview->viewport.is_valid() ? RenderingServer3D::get_singleton()->viewport_get_texture(preview->viewport) : preview->target;
			// Rectangle simple : avec des coins arrondis, le liseré d'anticrénelage échantillonne
			// hors de la texture de rendu et laisse un trait clair avec certains pilotes.
			dl->AddImage(_tex(texture), pos, end);
		}
		return true;
	}
	if (preview->kind == PREVIEW_AUDIO && !preview->peaks.empty()) {
		// Silhouette du son.
		dl->AddRectFilled(box_min, box_max, u32(palette.bg_dark), rounding);
		const int columns = (int)preview->peaks.size();
		const float middle = (box_min.y + box_max.y) * 0.5f;
		const float column_w = (box_w - ES(8)) / (float)columns;
		for (int i = 0; i < columns; i++) {
			const float half = std::max(1.0f, preview->peaks[i] * (box_h * 0.5f - ES(4)));
			const float x = box_min.x + ES(4) + (float)i * column_w;
			dl->AddRectFilled(ImVec2(x, middle - half), ImVec2(x + std::max(1.0f, column_w - 1.0f), middle + half), u32(palette.file_audio));
		}
		return true;
	}
	if (preview->kind == PREVIEW_TEXT && !preview->lines.empty() && box_h >= ES(40)) {
		// Premières lignes, en petit : on reconnaît la forme du fichier.
		dl->AddRectFilled(box_min, box_max, u32(palette.bg_dark), rounding);
		const float font_size = std::clamp(box_h / 9.0f, ES(7), ES(13));
		ImFont *font = font_mono ? font_mono : ImGui::GetFont();
		const ImVec4 clip(box_min.x + ES(4), box_min.y + ES(3), box_max.x - ES(4), box_max.y - ES(3));
		float y = box_min.y + ES(4);
		for (const String &line : preview->lines) {
			if (y + font_size > box_max.y - ES(3)) {
				break;
			}
			dl->AddText(font, font_size, ImVec2(box_min.x + ES(5), y), u32(path_get_extension(p_path) == "mx" ? palette.file_script : palette.text_dim), line.c_str(), nullptr, 0.0f, &clip);
			y += font_size * 1.18f;
		}
		return true;
	}
	return false;
}

void EditorNode::_draw_preview_tooltip(const String &p_path) {
	if (!ImGui::BeginTooltip()) {
		return;
	}
	FilePreview *preview = _get_preview(p_path);
	const float side = ES(168);
	if (preview && preview->ready && (_has_preview_image(preview) || !preview->peaks.empty() || !preview->lines.empty())) {
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const float height = preview->kind == PREVIEW_AUDIO ? ES(64) : side;
		const float width = preview->kind == PREVIEW_TEXT ? ES(300) : side;
		ImGui::Dummy(ImVec2(width, height));
		_draw_preview(p_path, Vector2(pos.x, pos.y), Vector2(pos.x + width, pos.y + height));
	}
	ImGui::TextUnformatted(path_get_file(p_path).c_str());
	if (preview && preview->ready && !preview->info.empty()) {
		ImGui::TextDisabled("%s", preview->info.c_str());
	}
	ImGui::TextDisabled("%s", p_path.c_str());
	ImGui::EndTooltip();
}

// ---------------------------------------------------------------------------
// Inspecteur : aperçu d'une ressource
// ---------------------------------------------------------------------------
void EditorNode::_draw_resource_preview(const Ref<Resource> &p_resource) {
	if (p_resource.is_null() || !pref_thumbnails) {
		return;
	}
	const float avail = ImGui::GetContentRegionAvail().x;
	ImDrawList *dl = ImGui::GetWindowDrawList();

	// --- Texture : l'image, sur damier ---
	if (Texture2D *texture = Object::cast_to<Texture2D>(p_resource.ptr())) {
		if (texture->get_width() <= 0 || texture->get_height() <= 0) {
			return;
		}
		const float box_h = ES(120);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(avail, box_h));
		dl->AddRectFilled(pos, ImVec2(pos.x + avail, pos.y + box_h), u32(palette.frame), ImGui::GetStyle().FrameRounding);
		const Size2 size = texture->get_size();
		float scale = std::min((avail - ES(12)) / size.x, (box_h - ES(12)) / size.y);
		if (scale > 1.0f) {
			scale = std::min(std::floor(scale), 8.0f);
		}
		const float w = std::floor(size.x * scale), h = std::floor(size.y * scale);
		const ImVec2 image_pos(std::floor(pos.x + (avail - w) * 0.5f), std::floor(pos.y + (box_h - h) * 0.5f));
		const ImVec2 image_end(image_pos.x + w, image_pos.y + h);
		_checkerboard(dl, image_pos, image_end, palette.dark);
		dl->AddImage(_tex(texture->get_rid()), image_pos, image_end);
		ImGui::TextDisabled("%d x %d px", texture->get_width(), texture->get_height());
		return;
	}

	// --- Maillage ou matériau : rendu 3D, à faire tourner à la souris ---
	Mesh *mesh = Object::cast_to<Mesh>(p_resource.ptr());
	Material *material = Object::cast_to<Material>(p_resource.ptr());
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	if ((!mesh && !material) || !rs3d || !rs3d->is_available()) {
		return;
	}
	const float side = std::min(avail, ES(150));
	LivePreview &live = live_previews[p_resource->get_instance_id()];
	if (live.last_used == 0 && material) {
		live.yaw = 0.0f;
		live.pitch = -0.15f;
	}
	live.last_used = preview_frame;
	live.mesh = mesh ? p_resource : Ref<Resource>();
	live.material = material ? p_resource : Ref<Resource>();
	live.size = std::max(32, (int)std::round(side * pixel_ratio));

	const ImVec2 pos(ImGui::GetCursorScreenPos().x + std::floor((avail - side) * 0.5f), ImGui::GetCursorScreenPos().y);
	ImGui::SetCursorScreenPos(pos);
	ImGui::InvisibleButton("##preview3d", ImVec2(side, side));
	if (ImGui::IsItemActive()) {
		const ImVec2 delta = ImGui::GetIO().MouseDelta;
		live.yaw -= delta.x * 0.012f;
		live.pitch = std::clamp(live.pitch - delta.y * 0.012f, -1.5f, 1.5f);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
	ImGui::SetItemTooltip("Glissez pour faire tourner l'aperçu");
	const ImVec2 end(pos.x + side, pos.y + side);
	const float rounding = ImGui::GetStyle().FrameRounding;
	if (live.viewport.is_valid()) {
		dl->AddImage(_tex(rs3d->viewport_get_texture(live.viewport)), pos, end);
	} else {
		dl->AddRectFilled(pos, end, u32(palette.frame), rounding);
	}
	dl->AddRect(pos, end, u32(palette.border));
	if (mesh) {
		const String info = vformat("%s triangles", _thousands(mesh->get_triangle_count()).c_str());
		const ImVec2 size = ImGui::CalcTextSize(info.c_str());
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - size.x) * 0.5f));
		ImGui::TextDisabled("%s", info.c_str());
	}
}
