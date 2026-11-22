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

	enum BlendMode {
		BLEND_MODE_MIX,
		BLEND_MODE_ADD,
		BLEND_MODE_SUB,
		BLEND_MODE_MUL,
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
		bool clip = false; // découpe l'item et ses enfants à clip_rect (coordonnées locales)
		Rect2 clip_rect;
		RID material;
		// Racine de canevas : teinte ambiante de l'éclairage 2D.
		bool lit = false;
		Color ambient = Color(1, 1, 1, 1);
	};

	// Matériau de canevas : mode de mélange et shader facultatif (rendering_server_fx.cpp).
	struct CanvasMaterial {
		BlendMode blend_mode = BLEND_MODE_MIX;
		String code;
		bool dirty = true;
		unsigned int program = 0;
		String error;
		std::map<String, Variant> params;
		std::map<String, RID> texture_params;
		std::map<String, int> uniform_types;
	};

	struct CanvasLight {
		RID canvas;
		bool enabled = true;
		Vector2 position;
		RID texture; // vide : dégradé radial intégré
		real_t scale = 1.0f;
		Color color = Color(1, 1, 1, 1);
		real_t energy = 1.0f;
		bool shadow_enabled = false;
	};

	struct CanvasOccluder {
		RID canvas;
		bool enabled = true;
		bool closed = true;
		Transform2D xform;
		std::vector<Vector2> polygon;
	};

	struct DrawEntry {
		Item *item;
		Transform2D xform;
		Color modulate;
		int z;
		bool clip = false;
		Rect2 clip_rect; // en coordonnées d'écran
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
	void _collect(Item *p_item, const Transform2D &p_xform, const Color &p_modulate, int p_z, const Rect2 *p_clip);

	std::unordered_map<uint64_t, CanvasMaterial> materials;
	std::unordered_map<uint64_t, CanvasLight> lights;
	std::unordered_map<uint64_t, CanvasOccluder> occluders;
	RID light_texture; // dégradé radial par défaut
	SDL_Texture *lightmap = nullptr, *light_scratch = nullptr;
	int lightmap_w = 0, lightmap_h = 0;
	bool shader_warning_shown = false;

	void _free_fx();
	bool _shaders_available() const;
	bool _material_prepare(CanvasMaterial &p_material);
	bool _draw_command_shader(const DrawEntry &p_entry, const Command &p_command, CanvasMaterial &p_material);
	void _draw_lighting(Item *p_canvas, const Transform2D &p_xform);
	void _apply_blend_mode(SDL_Texture *p_texture, BlendMode p_mode);
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

	// Découpe : rien n'est dessiné hors de p_rect (repère de l'item), enfants compris.
	void canvas_item_set_clip(RID p_item, bool p_enabled, const Rect2 &p_rect = Rect2());
	void canvas_item_set_material(RID p_item, RID p_material);

	// --- Matériaux de canevas : mélange et shaders ---
	RID canvas_material_create();
	void canvas_material_set_blend_mode(RID p_material, BlendMode p_mode);
	void canvas_material_set_shader_code(RID p_material, const String &p_code);
	void canvas_material_set_param(RID p_material, const String &p_name, const Variant &p_value);
	void canvas_material_set_texture_param(RID p_material, const String &p_name, RID p_texture);
	void canvas_material_clear_params(RID p_material);
	// Compile le shader si nécessaire ; retourne le message d'erreur ("" si tout va bien).
	String canvas_material_get_shader_error(RID p_material);
	bool canvas_shaders_available() const { return _shaders_available(); }

	// --- Éclairage 2D ---
	// Une teinte ambiante sombre et des lumières : le canevas est multiplié par la carte de
	// lumière. Les occulteurs projettent des ombres pour les lumières qui les demandent.
	void canvas_set_ambient(RID p_canvas, bool p_enabled, const Color &p_color = Color(1, 1, 1, 1));
	RID canvas_light_create();
	void canvas_light_set_canvas(RID p_light, RID p_canvas);
	void canvas_light_set_enabled(RID p_light, bool p_enabled);
	void canvas_light_set_position(RID p_light, const Vector2 &p_position);
	void canvas_light_set_texture(RID p_light, RID p_texture);
	void canvas_light_set_scale(RID p_light, real_t p_scale);
	void canvas_light_set_color(RID p_light, const Color &p_color);
	void canvas_light_set_energy(RID p_light, real_t p_energy);
	void canvas_light_set_shadow_enabled(RID p_light, bool p_enabled);
	RID canvas_occluder_create();
	void canvas_occluder_set_canvas(RID p_occluder, RID p_canvas);
	void canvas_occluder_set_enabled(RID p_occluder, bool p_enabled);
	void canvas_occluder_set_transform(RID p_occluder, const Transform2D &p_xform);
	void canvas_occluder_set_polygon(RID p_occluder, const std::vector<Vector2> &p_points, bool p_closed);

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
	// RID() = écran. p_scale : facteur appliqué à tout ce qui est dessiné dans la cible (une
	// cible deux fois plus grande que la résolution logique se dessine avec p_scale = 2).
	void set_render_target(RID p_texture, float p_scale = 1.0f);
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
