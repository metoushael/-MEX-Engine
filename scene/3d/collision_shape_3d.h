// CollisionShape3D : attache une forme (Shape3D) au corps ou à la zone parente.
// RayCast3D : lance un rayon à chaque pas de physique et rapporte le premier obstacle.
#pragma once

#include "scene/3d/node_3d.h"
#include "scene/resources/shape_3d.h"

class CollisionShape3D : public Node3D {
	GDCLASS(CollisionShape3D, Node3D)

	Ref<Shape3D> shape;
	bool disabled = false;
	Color debug_color = Color(0.0f, 0.75f, 0.85f, 1.0f);

protected:
	static void _bind_methods();

public:
	void set_shape(const Ref<Shape3D> &p_shape) { shape = p_shape; }
	Ref<Shape3D> get_shape() const { return shape; }
	void set_disabled(bool p_disabled) { disabled = p_disabled; }
	bool is_disabled() const { return disabled; }
	void set_debug_color(const Color &p_color) { debug_color = p_color; }
	Color get_debug_color() const { return debug_color; }

	virtual bool _edit_get_aabb(AABB &r_aabb) const override;
	virtual void _edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const override;
	virtual void _get_debug_collision_lines(std::vector<RenderingServer3D::Line> *r_lines) const override { _edit_get_gizmo_lines(r_lines); }
};

class RayCast3D : public Node3D {
	GDCLASS(RayCast3D, Node3D)

	Vector3 target_position = Vector3(0, -1, 0);
	bool enabled = true;
	bool exclude_parent = true;
	bool collide_with_bodies = true;
	bool collide_with_areas = false;
	uint32_t collision_mask = 1;

	bool colliding = false;
	ObjectID collider_id = 0;
	Vector3 collision_point;
	Vector3 collision_normal;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_target_position(const Vector3 &p_target) { target_position = p_target; }
	Vector3 get_target_position() const { return target_position; }
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }
	void set_exclude_parent(bool p_exclude) { exclude_parent = p_exclude; }
	bool get_exclude_parent() const { return exclude_parent; }
	void set_collide_with_bodies(bool p_enabled) { collide_with_bodies = p_enabled; }
	bool is_collide_with_bodies_enabled() const { return collide_with_bodies; }
	void set_collide_with_areas(bool p_enabled) { collide_with_areas = p_enabled; }
	bool is_collide_with_areas_enabled() const { return collide_with_areas; }
	void set_collision_mask(int p_mask) { collision_mask = (uint32_t)p_mask; }
	int get_collision_mask() const { return (int)collision_mask; }

	void force_raycast_update();
	bool is_colliding() const { return colliding; }
	Object *get_collider() const { return ObjectDB::get_instance(collider_id); }
	Vector3 get_collision_point() const { return collision_point; }
	Vector3 get_collision_normal() const { return collision_normal; }

	virtual void _edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const override;
	virtual void _get_debug_collision_lines(std::vector<RenderingServer3D::Line> *r_lines) const override { _edit_get_gizmo_lines(r_lines); }
};
