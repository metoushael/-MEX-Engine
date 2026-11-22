// Node2D : nœud 2D avec position, rotation et échelle.
#pragma once

#include "scene/main/canvas_item.h"

class Node2D : public CanvasItem {
	GDCLASS(Node2D, CanvasItem)

	Vector2 position;
	real_t rotation = 0;
	Vector2 scale = Vector2(1, 1);

protected:
	static void _bind_methods();

public:
	void set_position(const Vector2 &p_pos);
	Vector2 get_position() const { return position; }
	void set_rotation(real_t p_radians);
	real_t get_rotation() const { return rotation; }
	void set_rotation_degrees(real_t p_degrees) { set_rotation((real_t)Math::deg_to_rad(p_degrees)); }
	real_t get_rotation_degrees() const { return (real_t)Math::rad_to_deg(rotation); }
	void set_scale(const Vector2 &p_scale);
	Vector2 get_scale() const { return scale; }

	virtual Transform2D get_transform() const override { return Transform2D(rotation, scale, position); }
	void set_transform(const Transform2D &p_xform);
	void set_global_transform(const Transform2D &p_xform);

	void set_global_position(const Vector2 &p_pos);
	Vector2 get_global_position() const { return get_global_transform().origin; }
	void set_global_rotation(real_t p_radians);
	real_t get_global_rotation() const { return get_global_transform().get_rotation(); }
	Vector2 get_global_scale() const { return get_global_transform().get_scale(); }

	void translate(const Vector2 &p_offset) { set_position(position + p_offset); }
	void rotate(real_t p_radians) { set_rotation(rotation + p_radians); }
	void move_local_x(real_t p_delta) { set_position(position + Vector2::from_angle(rotation) * p_delta); }
	void move_local_y(real_t p_delta) { set_position(position + Vector2::from_angle(rotation + (real_t)Math_PI * 0.5f) * p_delta); }
	void look_at(const Vector2 &p_point);
	real_t get_angle_to(const Vector2 &p_point) const;
	Vector2 to_local(const Vector2 &p_global) const { return get_global_transform().affine_inverse().xform(p_global); }
	Vector2 to_global(const Vector2 &p_local) const { return get_global_transform().xform(p_local); }
};

// Simple repère visible dans l'éditeur (point d'apparition, etc.).
class Marker2D : public Node2D {
	GDCLASS(Marker2D, Node2D)

protected:
	void _notification(int p_what);
	static void _bind_methods() {}
};
