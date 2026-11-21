// RenderingServer : rendu 2D par "canvas items" (listes de commandes), comme dans Godot.
// Les nœuds ne dessinent jamais directement : ils enregistrent des commandes auprès du
// serveur, qui les trie (z-index) et les envoie au GPU via SDL_Renderer.
#pragma once

#include "core/object/object.h"

struct SDL_Renderer;
struct SDL_Texture;

// Identifiant opaque d'une ressource détenue par un serveur.
struct RID {
	uint64_t id = 0;
	bool is_valid() const { return id != 0; }
	bool operator==(const RID &r) const { return id == r.id; }
	bool operator!=(const RID &r) const { return id != r.id; }
	bool operator<(const RID &r) const { return id < r.id; }
};

class RenderingServer : public Object {
	GDCLASS(RenderingServer, Object)

public:
	struct Vertex {
		Vector2 pos;
		Vector2 uv;
		Color color;
	};

	enum TextureFilter {
		TEXTURE_FILTER_NEAREST,
		TEXTURE_FILTER_LINEAR,
	};

private:
	struct Texture {
		SDL_Texture *tex = nullptr;
		int w = 0, h = 0;
	};

	struct Command {
		RID texture;
		std::vector<Vertex> verts;
		std::vector<int> indices;
	};

	struct Item {
		RID self;
		Item *parent = nullptr;
		std::vector<Item *> children;
		bool children_dirty = false;
		Transform2D xform;
		bool visible = true;
		Color modulate = Color(1, 1, 1, 1);
		Color self_modulate = Color(1, 1, 1, 1);
		int z_index = 0;
		bool z_relative = true;
		int draw_index = 0;
		std::vector<Command> commands;
	};

	struct DrawEntry {
		Item *item;
		Transform2D xform;
		Color modulate;
		int z;
	};

	static RenderingServer *singleton;

	SDL_Renderer *renderer = nullptr;
	uint64_t next_id = 1;
	std::unordered_map<uint64_t, Texture> textures;
	std::unordered_map<uint64_t, Item *> items;
	TextureFilter default_filter = TEXTURE_FILTER_LINEAR;
	std::vector<DrawEntry> draw_list;
	int stat_draw_calls = 0;
	int stat_items = 0;

	Item *_get_item(RID p_rid) const;
	Command &_get_command(Item *p_item, RID p_texture);
	void _collect(Item *p_item, const Transform2D &p_xform, const Color &p_modulate, int p_z);
	void _unparent(Item *p_item);

protected:
	static void _bind_methods() {}

public:
	static RenderingServer *get_singleton() { return singleton; }

	void init(SDL_Renderer *p_renderer);
	void finish();
	SDL_Renderer *get_renderer() const { return renderer; }

	// --- Textures ---
	void set_default_texture_filter(TextureFilter p_filter) { default_filter = p_filter; }
	RID texture_create(int p_width, int p_height, const uint8_t *p_rgba, int p_filter = -1);
	RID texture_create_target(int p_width, int p_height); // cible de rendu
	Size2 texture_get_size(RID p_texture) const;
	SDL_Texture *texture_get_native(RID p_texture) const;
	void texture_set_filter(RID p_texture, TextureFilter p_filter);

	// --- Canvas ---
	RID canvas_create(); // racine d'un arbre d'items
	RID canvas_item_create();
	void canvas_item_set_parent(RID p_item, RID p_parent);
	void canvas_item_set_transform(RID p_item, const Transform2D &p_xform);
	void canvas_item_set_visible(RID p_item, bool p_visible);
	void canvas_item_set_modulate(RID p_item, const Color &p_color);
	void canvas_item_set_self_modulate(RID p_item, const Color &p_color);
	void canvas_item_set_z_index(RID p_item, int p_z);
	void canvas_item_set_z_as_relative(RID p_item, bool p_relative);
	void canvas_item_set_draw_index(RID p_item, int p_index);

	void canvas_item_clear(RID p_item);
	void canvas_item_add_line(RID p_item, const Vector2 &p_from, const Vector2 &p_to, const Color &p_color, real_t p_width = 1.0f);
	void canvas_item_add_polyline(RID p_item, const std::vector<Vector2> &p_points, const Color &p_color, real_t p_width = 1.0f, bool p_closed = false);
	void canvas_item_add_rect(RID p_item, const Rect2 &p_rect, const Color &p_color);
	void canvas_item_add_circle(RID p_item, const Vector2 &p_pos, real_t p_radius, const Color &p_color);
	void canvas_item_add_polygon(RID p_item, const std::vector<Vector2> &p_points, const Color &p_color); // convexe
	void canvas_item_add_texture_rect(RID p_item, const Rect2 &p_rect, RID p_texture, const Color &p_modulate = Color(1, 1, 1, 1));
	void canvas_item_add_texture_rect_region(RID p_item, const Rect2 &p_rect, RID p_texture, const Rect2 &p_src_rect, const Color &p_modulate = Color(1, 1, 1, 1));
	void canvas_item_add_triangles(RID p_item, const std::vector<Vertex> &p_verts, const std::vector<int> &p_indices, RID p_texture);

	void free_rid(RID p_rid);

	// --- Frame ---
	void set_render_target(RID p_texture); // RID() = écran
	void clear(const Color &p_color);
	void draw_canvas(RID p_canvas, const Transform2D &p_xform);
	// Dessin immédiat (grille de l'éditeur...).
	void draw_line_immediate(const Vector2 &p_from, const Vector2 &p_to, const Color &p_color);
	void draw_rect_immediate(const Rect2 &p_rect, const Color &p_color, bool p_filled);
	// Copie opaque d'une texture (rendu 3D composé sous le canvas).
	void draw_texture_immediate(const Rect2 &p_rect, RID p_texture);

	void reset_stats() { stat_draw_calls = stat_items = 0; }
	int get_stat_draw_calls() const { return stat_draw_calls; }
	int get_stat_items() const { return stat_items; }

	RenderingServer();
	~RenderingServer();
};
