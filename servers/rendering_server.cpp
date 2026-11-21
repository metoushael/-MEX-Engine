#include "rendering_server.h"

#include <SDL.h>

RenderingServer *RenderingServer::singleton = nullptr;

RenderingServer::RenderingServer() {
	singleton = this;
}

RenderingServer::~RenderingServer() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void RenderingServer::init(SDL_Renderer *p_renderer) {
	renderer = p_renderer;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

void RenderingServer::finish() {
	for (auto &kv : items) {
		delete kv.second;
	}
	items.clear();
	for (auto &kv : textures) {
		if (kv.second.tex) {
			SDL_DestroyTexture(kv.second.tex);
		}
	}
	textures.clear();
	renderer = nullptr;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
RID RenderingServer::texture_create(int p_width, int p_height, const uint8_t *p_rgba, int p_filter) {
	ERR_FAIL_COND_V(!renderer || p_width <= 0 || p_height <= 0, RID());
	SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, p_width, p_height);
	ERR_FAIL_COND_V_MSG(!tex, RID(), String("SDL_CreateTexture: ") + SDL_GetError());
	if (p_rgba) {
		SDL_UpdateTexture(tex, nullptr, p_rgba, p_width * 4);
	}
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	TextureFilter filter = p_filter < 0 ? default_filter : (TextureFilter)p_filter;
	SDL_SetTextureScaleMode(tex, filter == TEXTURE_FILTER_NEAREST ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
	RID rid;
	rid.id = next_id++;
	Texture t;
	t.tex = tex;
	t.w = p_width;
	t.h = p_height;
	textures[rid.id] = t;
	return rid;
}

RID RenderingServer::texture_create_target(int p_width, int p_height) {
	ERR_FAIL_COND_V(!renderer || p_width <= 0 || p_height <= 0, RID());
	SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, p_width, p_height);
	ERR_FAIL_COND_V_MSG(!tex, RID(), String("SDL_CreateTexture (cible): ") + SDL_GetError());
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
	RID rid;
	rid.id = next_id++;
	Texture t;
	t.tex = tex;
	t.w = p_width;
	t.h = p_height;
	textures[rid.id] = t;
	return rid;
}

Size2 RenderingServer::texture_get_size(RID p_texture) const {
	auto it = textures.find(p_texture.id);
	return it == textures.end() ? Size2() : Size2((real_t)it->second.w, (real_t)it->second.h);
}

SDL_Texture *RenderingServer::texture_get_native(RID p_texture) const {
	auto it = textures.find(p_texture.id);
	return it == textures.end() ? nullptr : it->second.tex;
}

void RenderingServer::texture_set_filter(RID p_texture, TextureFilter p_filter) {
	auto it = textures.find(p_texture.id);
	if (it != textures.end()) {
		SDL_SetTextureScaleMode(it->second.tex, p_filter == TEXTURE_FILTER_NEAREST ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
	}
}

// ---------------------------------------------------------------------------
// Canvas items
// ---------------------------------------------------------------------------
RenderingServer::Item *RenderingServer::_get_item(RID p_rid) const {
	auto it = items.find(p_rid.id);
	return it == items.end() ? nullptr : it->second;
}

RID RenderingServer::canvas_create() {
	return canvas_item_create();
}

RID RenderingServer::canvas_item_create() {
	Item *item = new Item;
	item->self.id = next_id++;
	items[item->self.id] = item;
	return item->self;
}

void RenderingServer::_unparent(Item *p_item) {
	if (p_item->parent) {
		auto &siblings = p_item->parent->children;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), p_item), siblings.end());
		p_item->parent = nullptr;
	}
}

void RenderingServer::canvas_item_set_parent(RID p_item, RID p_parent) {
	Item *item = _get_item(p_item);
	if (!item) {
		return;
	}
	Item *parent = _get_item(p_parent);
	if (item->parent == parent) {
		return;
	}
	_unparent(item);
	if (parent) {
		item->parent = parent;
		parent->children.push_back(item);
		parent->children_dirty = true;
	}
}

void RenderingServer::canvas_item_set_transform(RID p_item, const Transform2D &p_xform) {
	if (Item *item = _get_item(p_item)) {
		item->xform = p_xform;
	}
}

void RenderingServer::canvas_item_set_visible(RID p_item, bool p_visible) {
	if (Item *item = _get_item(p_item)) {
		item->visible = p_visible;
	}
}

void RenderingServer::canvas_item_set_modulate(RID p_item, const Color &p_color) {
	if (Item *item = _get_item(p_item)) {
		item->modulate = p_color;
	}
}

void RenderingServer::canvas_item_set_self_modulate(RID p_item, const Color &p_color) {
	if (Item *item = _get_item(p_item)) {
		item->self_modulate = p_color;
	}
}

void RenderingServer::canvas_item_set_z_index(RID p_item, int p_z) {
	if (Item *item = _get_item(p_item)) {
		item->z_index = p_z;
	}
}

void RenderingServer::canvas_item_set_z_as_relative(RID p_item, bool p_relative) {
	if (Item *item = _get_item(p_item)) {
		item->z_relative = p_relative;
	}
}

void RenderingServer::canvas_item_set_draw_index(RID p_item, int p_index) {
	if (Item *item = _get_item(p_item)) {
		if (item->draw_index != p_index) {
			item->draw_index = p_index;
			if (item->parent) {
				item->parent->children_dirty = true;
			}
		}
	}
}

void RenderingServer::canvas_item_clear(RID p_item) {
	if (Item *item = _get_item(p_item)) {
		item->commands.clear();
	}
}

RenderingServer::Command &RenderingServer::_get_command(Item *p_item, RID p_texture) {
	// Fusionne les primitives consécutives partageant la même texture.
	if (p_item->commands.empty() || p_item->commands.back().texture != p_texture || p_item->commands.back().verts.size() > 16000) {
		Command cmd;
		cmd.texture = p_texture;
		p_item->commands.push_back(cmd);
	}
	return p_item->commands.back();
}

static void _push_quad(std::vector<RenderingServer::Vertex> &verts, std::vector<int> &indices, const Vector2 p[4], const Vector2 uv[4], const Color &c) {
	int base = (int)verts.size();
	for (int i = 0; i < 4; i++) {
		RenderingServer::Vertex v;
		v.pos = p[i];
		v.uv = uv[i];
		v.color = c;
		verts.push_back(v);
	}
	static const int quad[6] = { 0, 1, 2, 0, 2, 3 };
	for (int i = 0; i < 6; i++) {
		indices.push_back(base + quad[i]);
	}
}

void RenderingServer::canvas_item_add_line(RID p_item, const Vector2 &p_from, const Vector2 &p_to, const Color &p_color, real_t p_width) {
	Item *item = _get_item(p_item);
	if (!item) {
		return;
	}
	Vector2 dir = p_to - p_from;
	real_t len = dir.length();
	if (len <= 0) {
		return;
	}
	Vector2 n = Vector2(-dir.y, dir.x) / len * (std::max(p_width, 1.0f) * 0.5f);
	Vector2 p[4] = { p_from + n, p_to + n, p_to - n, p_from - n };
	Vector2 uv[4];
	Command &cmd = _get_command(item, RID());
	_push_quad(cmd.verts, cmd.indices, p, uv, p_color);
}

void RenderingServer::canvas_item_add_polyline(RID p_item, const std::vector<Vector2> &p_points, const Color &p_color, real_t p_width, bool p_closed) {
	for (size_t i = 0; i + 1 < p_points.size(); i++) {
		canvas_item_add_line(p_item, p_points[i], p_points[i + 1], p_color, p_width);
	}
	if (p_closed && p_points.size() > 2) {
		canvas_item_add_line(p_item, p_points.back(), p_points.front(), p_color, p_width);
	}
}

void RenderingServer::canvas_item_add_rect(RID p_item, const Rect2 &p_rect, const Color &p_color) {
	Item *item = _get_item(p_item);
	if (!item) {
		return;
	}
	Vector2 b = p_rect.position, e = p_rect.position + p_rect.size;
	Vector2 p[4] = { b, Vector2(e.x, b.y), e, Vector2(b.x, e.y) };
	Vector2 uv[4];
	Command &cmd = _get_command(item, RID());
	_push_quad(cmd.verts, cmd.indices, p, uv, p_color);
}

void RenderingServer::canvas_item_add_circle(RID p_item, const Vector2 &p_pos, real_t p_radius, const Color &p_color) {
	if (p_radius <= 0) {
		return;
	}
	int segments = Math::clamp((int)(p_radius * 0.8f) + 12, 12, 96);
	std::vector<Vector2> points;
	points.reserve(segments);
	for (int i = 0; i < segments; i++) {
		real_t a = (real_t)(Math_TAU * i / segments);
		points.push_back(p_pos + Vector2(std::cos(a), std::sin(a)) * p_radius);
	}
	canvas_item_add_polygon(p_item, points, p_color);
}

void RenderingServer::canvas_item_add_polygon(RID p_item, const std::vector<Vector2> &p_points, const Color &p_color) {
	Item *item = _get_item(p_item);
	if (!item || p_points.size() < 3) {
		return;
	}
	Command &cmd = _get_command(item, RID());
	int base = (int)cmd.verts.size();
	for (const Vector2 &pt : p_points) {
		Vertex v;
		v.pos = pt;
		v.color = p_color;
		cmd.verts.push_back(v);
	}
	for (int i = 1; i + 1 < (int)p_points.size(); i++) {
		cmd.indices.push_back(base);
		cmd.indices.push_back(base + i);
		cmd.indices.push_back(base + i + 1);
	}
}

void RenderingServer::canvas_item_add_texture_rect(RID p_item, const Rect2 &p_rect, RID p_texture, const Color &p_modulate) {
	Size2 size = texture_get_size(p_texture);
	canvas_item_add_texture_rect_region(p_item, p_rect, p_texture, Rect2(Vector2(), size), p_modulate);
}

void RenderingServer::canvas_item_add_texture_rect_region(RID p_item, const Rect2 &p_rect, RID p_texture, const Rect2 &p_src_rect, const Color &p_modulate) {
	Item *item = _get_item(p_item);
	Size2 size = texture_get_size(p_texture);
	if (!item || size.x <= 0 || size.y <= 0) {
		return;
	}
	// Une taille négative retourne naturellement l'image (comme dans Godot).
	Vector2 b = p_rect.position, e = p_rect.position + p_rect.size;
	Vector2 p[4] = { b, Vector2(e.x, b.y), e, Vector2(b.x, e.y) };
	Vector2 ub = p_src_rect.position / size, ue = (p_src_rect.position + p_src_rect.size) / size;
	Vector2 uv[4] = { ub, Vector2(ue.x, ub.y), ue, Vector2(ub.x, ue.y) };
	Command &cmd = _get_command(item, p_texture);
	_push_quad(cmd.verts, cmd.indices, p, uv, p_modulate);
}

void RenderingServer::canvas_item_add_triangles(RID p_item, const std::vector<Vertex> &p_verts, const std::vector<int> &p_indices, RID p_texture) {
	Item *item = _get_item(p_item);
	if (!item || p_verts.empty()) {
		return;
	}
	Command &cmd = _get_command(item, p_texture);
	int base = (int)cmd.verts.size();
	cmd.verts.insert(cmd.verts.end(), p_verts.begin(), p_verts.end());
	for (int idx : p_indices) {
		cmd.indices.push_back(base + idx);
	}
}

void RenderingServer::free_rid(RID p_rid) {
	if (!p_rid.is_valid()) {
		return;
	}
	auto tex_it = textures.find(p_rid.id);
	if (tex_it != textures.end()) {
		if (tex_it->second.tex) {
			SDL_DestroyTexture(tex_it->second.tex);
		}
		textures.erase(tex_it);
		return;
	}
	auto item_it = items.find(p_rid.id);
	if (item_it != items.end()) {
		Item *item = item_it->second;
		_unparent(item);
		for (Item *child : item->children) {
			child->parent = nullptr;
		}
		delete item;
		items.erase(item_it);
	}
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------
void RenderingServer::set_render_target(RID p_texture, float p_scale) {
	SDL_SetRenderTarget(renderer, p_texture.is_valid() ? texture_get_native(p_texture) : nullptr);
	if (p_texture.is_valid()) {
		// L'échelle est propre à chaque cible : SDL la remet à 1 en changeant de cible.
		SDL_RenderSetScale(renderer, p_scale, p_scale);
	}
}

static inline SDL_Color _sdl_color(const Color &c) {
	SDL_Color out;
	out.r = (Uint8)(Math::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
	out.g = (Uint8)(Math::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
	out.b = (Uint8)(Math::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
	out.a = (Uint8)(Math::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
	return out;
}

void RenderingServer::clear(const Color &p_color) {
	SDL_Color c = _sdl_color(p_color);
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
	SDL_RenderClear(renderer);
}

void RenderingServer::_collect(Item *p_item, const Transform2D &p_xform, const Color &p_modulate, int p_z) {
	if (!p_item->visible) {
		return;
	}
	Transform2D xform = p_xform * p_item->xform;
	Color modulate = p_modulate * p_item->modulate;
	int z = p_item->z_relative ? p_z + p_item->z_index : p_item->z_index;
	if (!p_item->commands.empty()) {
		DrawEntry entry;
		entry.item = p_item;
		entry.xform = xform;
		entry.modulate = modulate * p_item->self_modulate;
		entry.z = z;
		draw_list.push_back(entry);
	}
	if (p_item->children_dirty) {
		std::stable_sort(p_item->children.begin(), p_item->children.end(), [](const Item *a, const Item *b) {
			return a->draw_index < b->draw_index;
		});
		p_item->children_dirty = false;
	}
	for (Item *child : p_item->children) {
		_collect(child, xform, modulate, z);
	}
}

void RenderingServer::draw_canvas(RID p_canvas, const Transform2D &p_xform) {
	Item *canvas = _get_item(p_canvas);
	if (!canvas || !renderer) {
		return;
	}
	draw_list.clear();
	_collect(canvas, p_xform, Color(1, 1, 1, 1), 0);
	std::stable_sort(draw_list.begin(), draw_list.end(), [](const DrawEntry &a, const DrawEntry &b) {
		return a.z < b.z;
	});

	static std::vector<SDL_Vertex> buffer;
	for (const DrawEntry &entry : draw_list) {
		if (entry.modulate.a <= 0.0f) {
			continue;
		}
		stat_items++;
		for (const Command &cmd : entry.item->commands) {
			if (cmd.indices.empty()) {
				continue;
			}
			buffer.resize(cmd.verts.size());
			for (size_t i = 0; i < cmd.verts.size(); i++) {
				const Vertex &v = cmd.verts[i];
				Vector2 p = entry.xform.xform(v.pos);
				buffer[i].position.x = p.x;
				buffer[i].position.y = p.y;
				buffer[i].tex_coord.x = v.uv.x;
				buffer[i].tex_coord.y = v.uv.y;
				buffer[i].color = _sdl_color(v.color * entry.modulate);
			}
			SDL_Texture *tex = cmd.texture.is_valid() ? texture_get_native(cmd.texture) : nullptr;
			if (cmd.texture.is_valid() && !tex) {
				continue; // texture libérée
			}
			SDL_RenderGeometry(renderer, tex, buffer.data(), (int)buffer.size(), cmd.indices.data(), (int)cmd.indices.size());
			stat_draw_calls++;
		}
	}
}

void RenderingServer::draw_line_immediate(const Vector2 &p_from, const Vector2 &p_to, const Color &p_color) {
	SDL_Color c = _sdl_color(p_color);
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
	SDL_RenderDrawLineF(renderer, p_from.x, p_from.y, p_to.x, p_to.y);
}

void RenderingServer::draw_rect_immediate(const Rect2 &p_rect, const Color &p_color, bool p_filled) {
	SDL_Color c = _sdl_color(p_color);
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
	SDL_FRect r = { p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y };
	if (p_filled) {
		SDL_RenderFillRectF(renderer, &r);
	} else {
		SDL_RenderDrawRectF(renderer, &r);
	}
}

void RenderingServer::draw_texture_immediate(const Rect2 &p_rect, RID p_texture) {
	SDL_Texture *tex = texture_get_native(p_texture);
	if (!tex) {
		return;
	}
	const SDL_FRect r = { p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y };
	SDL_RenderCopyF(renderer, tex, nullptr, &r);
}
