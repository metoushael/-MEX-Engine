// Éclairage 2D : CanvasModulate (teinte ambiante), PointLight2D (lumière) et
// LightOccluder2D (obstacle qui projette une ombre).
#pragma once

#include "scene/2d/node_2d.h"

// Assombrit ou teinte tout le canevas ; les lumières éclaircissent ensuite par endroits.
class CanvasModulate : public Node2D {
	GDCLASS(CanvasModulate, Node2D)

	Color color = Color(1, 1, 1, 1);
	RID canvas;

	void _update();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_color(const Color &p_color);
	Color get_color() const { return color; }
};

class PointLight2D : public Node2D {
	GDCLASS(PointLight2D, Node2D)

	RID light;
	bool enabled = true;
	Ref<Texture2D> texture; // vide : halo rond intégré de 256 pixels
	real_t texture_scale = 1.0f;
	Color color = Color(1, 1, 1, 1);
	real_t energy = 1.0f;
	bool shadow_enabled = false;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const { return texture; }
	void set_texture_scale(real_t p_scale);
	real_t get_texture_scale() const { return texture_scale; }
	void set_color(const Color &p_color);
	Color get_color() const { return color; }
	void set_energy(real_t p_energy);
	real_t get_energy() const { return energy; }
	void set_shadow_enabled(bool p_enabled);
	bool is_shadow_enabled() const { return shadow_enabled; }

	virtual Rect2 _edit_get_rect() const override { return Rect2(-12, -12, 24, 24); }

	PointLight2D();
	~PointLight2D();
};

class LightOccluder2D : public Node2D {
	GDCLASS(LightOccluder2D, Node2D)

	RID occluder;
	Array polygon; // points (Vector2) dans le repère du nœud
	bool closed = true;
	bool enabled = true;

	void _update_polygon();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_polygon(const Array &p_polygon);
	Array get_polygon() const { return polygon; }
	void set_closed(bool p_closed);
	bool is_closed() const { return closed; }
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }

	LightOccluder2D();
	~LightOccluder2D();
};
