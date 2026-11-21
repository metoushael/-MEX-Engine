// RayCast2D : lance un rayon à chaque pas de physique et rapporte le premier obstacle.
#pragma once

#include "scene/2d/node_2d.h"

class RayCast2D : public Node2D {
	GDCLASS(RayCast2D, Node2D)

	Vector2 target_position = Vector2(0, 50);
	bool enabled = true;
	bool exclude_parent = true;
	bool collide_with_bodies = true;
	bool collide_with_areas = false;
	uint32_t collision_mask = 1;

	bool colliding = false;
	ObjectID collider_id = 0;
	Vector2 collision_point;
	Vector2 collision_normal;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_target_position(const Vector2 &p_target);
	Vector2 get_target_position() const { return target_position; }
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
	Vector2 get_collision_point() const { return collision_point; }
	Vector2 get_collision_normal() const { return collision_normal; }

	virtual bool _edit_use_rect() const override { return true; }
	virtual Rect2 _edit_get_rect() const override { return Rect2(Vector2(), Size2()).expand(target_position).grow(4); }
};
