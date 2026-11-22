// CanvasItem : base de tout ce qui se dessine en 2D (Node2D et Control).
#pragma once

#include "scene/main/node.h"
#include "scene/resources/canvas_material.h"
#include "scene/resources/font.h"
#include "scene/resources/texture.h"
#include "servers/rendering_server.h"

class CanvasLayer;

class CanvasItem : public Node {
	GDCLASS(CanvasItem, Node)

public:
	enum {
		NOTIFICATION_DRAW = 30,
		NOTIFICATION_VISIBILITY_CHANGED = 31,
	};

private:
	RID canvas_item;
	bool visible = true;
	Color modulate = Color(1, 1, 1, 1);
	Color self_modulate = Color(1, 1, 1, 1);
	int z_index = 0;
	bool z_as_relative = true;
	bool pending_redraw = false;
	bool drawing = false;
	Ref<CanvasItemMaterial> material;

	static std::vector<ObjectID> redraw_queue;

	void _redraw();
	void _draw_polygon_bind(const Array &p_points, const Color &p_color);
	void _draw_polyline_bind(const Array &p_points, const Color &p_color, real_t p_width);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	void _update_canvas_transform(); // envoie get_transform() au serveur

public:
	RID get_canvas_item() const { return canvas_item; }

	virtual Transform2D get_transform() const { return Transform2D(); }
	Transform2D get_global_transform() const;
	CanvasLayer *get_canvas_layer_node() const;
	// Transformation du canevas (caméra ou CanvasLayer) vers l'écran.
	Transform2D get_canvas_transform() const;
	Vector2 get_global_mouse_position() const;
	Vector2 get_local_mouse_position() const;
	Size2 get_viewport_size() const;

	void set_visible(bool p_visible);
	bool is_visible() const { return visible; }
	bool is_visible_in_tree() const;
	void show() { set_visible(true); }
	void hide() { set_visible(false); }
	void set_modulate(const Color &p_color);
	Color get_modulate() const { return modulate; }
	void set_self_modulate(const Color &p_color);
	Color get_self_modulate() const { return self_modulate; }
	void set_z_index(int p_z);
	int get_z_index() const { return z_index; }
	void set_z_as_relative(bool p_relative);
	bool is_z_relative() const { return z_as_relative; }
	// Matériau : mode de mélange (CanvasItemMaterial) ou shader (ShaderMaterial).
	void set_material(const Ref<CanvasItemMaterial> &p_material);
	Ref<CanvasItemMaterial> get_material() const { return material; }
	// Découpe ce nœud et ses enfants à un rectangle exprimé dans son repère.
	void set_clip_rect(bool p_enabled, const Rect2 &p_rect = Rect2());

	// --- Dessin (valide uniquement pendant NOTIFICATION_DRAW / _draw) ---
	void queue_redraw();
	void draw_line(const Vector2 &p_from, const Vector2 &p_to, const Color &p_color, real_t p_width = 1.0f);
	void draw_rect(const Rect2 &p_rect, const Color &p_color, bool p_filled = true, real_t p_width = 1.0f);
	void draw_circle(const Vector2 &p_pos, real_t p_radius, const Color &p_color);
	void draw_arc(const Vector2 &p_center, real_t p_radius, real_t p_start, real_t p_end, int p_points, const Color &p_color, real_t p_width = 1.0f);
	void draw_colored_polygon(const std::vector<Vector2> &p_points, const Color &p_color);
	void draw_polyline(const std::vector<Vector2> &p_points, const Color &p_color, real_t p_width = 1.0f, bool p_closed = false);
	void draw_texture(const Ref<Texture2D> &p_texture, const Vector2 &p_pos, const Color &p_modulate = Color(1, 1, 1, 1));
	void draw_texture_rect(const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Color &p_modulate = Color(1, 1, 1, 1));
	void draw_texture_rect_region(const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Rect2 &p_src_rect, const Color &p_modulate = Color(1, 1, 1, 1));
	void draw_string(const Ref<Font> &p_font, const Vector2 &p_pos, const String &p_text, int p_font_size = 16, const Color &p_color = Color(1, 1, 1, 1));

	static void flush_redraws();

	// --- Aides pour l'éditeur ---
	virtual bool _edit_use_rect() const { return false; }
	virtual Rect2 _edit_get_rect() const { return Rect2(-8, -8, 16, 16); }

	CanvasItem();
	~CanvasItem();
};
