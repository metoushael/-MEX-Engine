// Control : base des éléments d'interface.
// Le rectangle (position + taille) est calculé à partir d'ancres (fractions de la zone du
// parent) et de marges en pixels, comme dans Godot : gauche = ancre * largeur_parent + marge.
#pragma once

#include "scene/main/canvas_item.h"

class Control : public CanvasItem {
	GDCLASS(Control, CanvasItem)

public:
	enum {
		NOTIFICATION_RESIZED = 40,
		NOTIFICATION_MOUSE_ENTER = 41,
		NOTIFICATION_MOUSE_EXIT = 42,
		NOTIFICATION_SORT_CHILDREN = 43,
	};

	enum Side {
		SIDE_LEFT,
		SIDE_TOP,
		SIDE_RIGHT,
		SIDE_BOTTOM,
	};

	enum LayoutPreset {
		PRESET_TOP_LEFT,
		PRESET_TOP_RIGHT,
		PRESET_BOTTOM_LEFT,
		PRESET_BOTTOM_RIGHT,
		PRESET_CENTER_LEFT,
		PRESET_CENTER_TOP,
		PRESET_CENTER_RIGHT,
		PRESET_CENTER_BOTTOM,
		PRESET_CENTER,
		PRESET_LEFT_WIDE,
		PRESET_TOP_WIDE,
		PRESET_RIGHT_WIDE,
		PRESET_BOTTOM_WIDE,
		PRESET_FULL_RECT,
		PRESET_CUSTOM,
	};

	// Comportement dans un conteneur.
	enum SizeFlags {
		SIZE_FILL,
		SIZE_EXPAND_FILL,
		SIZE_SHRINK_BEGIN,
		SIZE_SHRINK_CENTER,
		SIZE_SHRINK_END,
	};

private:
	Vector2 position;
	Vector2 size = Vector2(100, 40);
	real_t anchors[4] = { 0, 0, 0, 0 };
	real_t offsets[4] = { 0, 0, 100, 40 };
	Vector2 custom_minimum_size;
	int size_flags_h = SIZE_FILL;
	int size_flags_v = SIZE_FILL;
	bool updating_rect = false;

	void _compute_offsets(); // marges <- rectangle courant
	void _apply_rect(const Vector2 &p_position, const Vector2 &p_size);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	// Position d'un événement souris dans le repère local du contrôle.
	Vector2 _event_to_local(const Vector2 &p_screen_pos) const;

public:
	void set_position(const Vector2 &p_pos);
	Vector2 get_position() const { return position; }
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const { return size; }
	void set_global_position(const Vector2 &p_pos);
	Vector2 get_global_position() const { return get_global_transform().origin; }
	Rect2 get_rect() const { return Rect2(position, size); }
	Rect2 get_global_rect() const { return Rect2(get_global_position(), size); }
	bool has_point(const Vector2 &p_local) const { return Rect2(Vector2(), size).has_point(p_local); }

	// Zone de référence des ancres : taille du Control parent, sinon celle de l'écran du jeu.
	Size2 get_parent_area_size() const;
	void update_rect_from_anchors(); // rectangle <- ancres + marges

	void set_anchor(Side p_side, real_t p_anchor);
	real_t get_anchor(Side p_side) const { return anchors[p_side]; }
	void set_offset(Side p_side, real_t p_offset);
	real_t get_offset(Side p_side) const { return offsets[p_side]; }
	void set_anchors_preset(int p_preset);
	int get_anchors_preset() const;

	void set_anchor_left(real_t v) { set_anchor(SIDE_LEFT, v); }
	real_t get_anchor_left() const { return anchors[SIDE_LEFT]; }
	void set_anchor_top(real_t v) { set_anchor(SIDE_TOP, v); }
	real_t get_anchor_top() const { return anchors[SIDE_TOP]; }
	void set_anchor_right(real_t v) { set_anchor(SIDE_RIGHT, v); }
	real_t get_anchor_right() const { return anchors[SIDE_RIGHT]; }
	void set_anchor_bottom(real_t v) { set_anchor(SIDE_BOTTOM, v); }
	real_t get_anchor_bottom() const { return anchors[SIDE_BOTTOM]; }
	void set_offset_left(real_t v) { set_offset(SIDE_LEFT, v); }
	real_t get_offset_left() const { return offsets[SIDE_LEFT]; }
	void set_offset_top(real_t v) { set_offset(SIDE_TOP, v); }
	real_t get_offset_top() const { return offsets[SIDE_TOP]; }
	void set_offset_right(real_t v) { set_offset(SIDE_RIGHT, v); }
	real_t get_offset_right() const { return offsets[SIDE_RIGHT]; }
	void set_offset_bottom(real_t v) { set_offset(SIDE_BOTTOM, v); }
	real_t get_offset_bottom() const { return offsets[SIDE_BOTTOM]; }

	void set_custom_minimum_size(const Vector2 &p_size);
	Vector2 get_custom_minimum_size() const { return custom_minimum_size; }
	void set_h_size_flags(int p_flags);
	int get_h_size_flags() const { return size_flags_h; }
	void set_v_size_flags(int p_flags);
	int get_v_size_flags() const { return size_flags_v; }

	virtual Transform2D get_transform() const override { return Transform2D(Vector2(1, 0), Vector2(0, 1), position); }
	virtual Size2 get_minimum_size() const { return Size2(); }
	virtual bool is_container() const { return false; }
	Size2 get_combined_minimum_size() const;
	// À appeler quand la taille minimale change (texte, police...) : prévient le conteneur parent.
	void update_minimum_size();
	// Utilisé par les conteneurs : place l'enfant sans toucher à ses ancres.
	void set_rect_in_container(const Rect2 &p_rect);

	virtual bool _edit_use_rect() const override { return true; }
	virtual Rect2 _edit_get_rect() const override { return Rect2(Vector2(), size); }
};

