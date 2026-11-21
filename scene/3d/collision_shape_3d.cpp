#include "collision_shape_3d.h"

#include "scene/3d/physics_body_3d.h"
#include "scene/main/scene_tree.h"

// ---------------------------------------------------------------------------
// CollisionShape3D
// ---------------------------------------------------------------------------
bool CollisionShape3D::_edit_get_aabb(AABB &r_aabb) const {
	if (shape.is_null()) {
		return false;
	}
	r_aabb = shape->get_aabb();
	return true;
}

void CollisionShape3D::_edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const {
	if (shape.is_null()) {
		return;
	}
	const Color color = disabled ? Color(0.6f, 0.6f, 0.6f, 1) : debug_color;
	shape->get_debug_lines(r_lines, get_global_transform(), color);
}

void CollisionShape3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shape", "shape"), &CollisionShape3D::set_shape);
	ClassDB::bind_method(D_METHOD("get_shape"), &CollisionShape3D::get_shape);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &CollisionShape3D::set_disabled);
	ClassDB::bind_method(D_METHOD("is_disabled"), &CollisionShape3D::is_disabled);
	ClassDB::bind_method(D_METHOD("set_debug_color", "color"), &CollisionShape3D::set_debug_color);
	ClassDB::bind_method(D_METHOD("get_debug_color"), &CollisionShape3D::get_debug_color);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shape", PROPERTY_HINT_RESOURCE_TYPE, "Shape3D"), "set_shape", "get_shape");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "disabled"), "set_disabled", "is_disabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "debug_color"), "set_debug_color", "get_debug_color");
}

// ---------------------------------------------------------------------------
// RayCast3D
// ---------------------------------------------------------------------------
void RayCast3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_physics_process_internal(enabled && !get_tree()->is_edit_mode());
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (enabled) {
				force_raycast_update();
			}
		} break;
	}
}

void RayCast3D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (is_inside_tree()) {
		set_physics_process_internal(enabled && !get_tree()->is_edit_mode());
	}
	if (!enabled) {
		colliding = false;
		collider_id = 0;
	}
}

void RayCast3D::force_raycast_update() {
	colliding = false;
	collider_id = 0;
	if (!is_inside_tree() || get_tree()->is_edit_mode()) {
		return;
	}
	const Transform3D global = get_global_transform();
	RID exclude;
	if (exclude_parent) {
		if (CollisionObject3D *co = Object::cast_to<CollisionObject3D>(get_parent())) {
			exclude = co->get_rid();
		}
	}
	PhysicsServer3D::RayResult result;
	if (PhysicsServer3D::get_singleton()->space_intersect_ray(get_tree()->get_space_3d(), global.origin, global.xform(target_position), result, collision_mask, exclude, collide_with_bodies, collide_with_areas)) {
		colliding = true;
		collider_id = result.collider_id;
		collision_point = result.position;
		collision_normal = result.normal;
	}
}

void RayCast3D::_edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const {
	Color color = colliding ? Color(1.0f, 0.3f, 0.2f, 1) : Color(0.0f, 0.7f, 0.9f, 1);
	if (!enabled) {
		color = Color(0.6f, 0.6f, 0.6f, 1);
	}
	const Transform3D global = get_global_transform();
	const Vector3 from = global.origin, to = global.xform(target_position);
	r_lines->push_back({ from, to, color });
	// Pointe de flèche : quatre arêtes repliées vers l'origine.
	const Vector3 dir = (to - from).normalized();
	if (dir.is_zero_approx()) {
		return;
	}
	Vector3 side = dir.cross(std::fabs(dir.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0)).normalized();
	const Vector3 other = dir.cross(side);
	const real_t length = std::min((to - from).length() * 0.25f, (real_t)0.15);
	const Vector3 sides[4] = { side, -side, other, -other };
	for (const Vector3 &s : sides) {
		r_lines->push_back({ to, to - dir * length + s * length * 0.5f, color });
	}
}

void RayCast3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_position", "target"), &RayCast3D::set_target_position);
	ClassDB::bind_method(D_METHOD("get_target_position"), &RayCast3D::get_target_position);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &RayCast3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &RayCast3D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_exclude_parent", "exclude"), &RayCast3D::set_exclude_parent);
	ClassDB::bind_method(D_METHOD("get_exclude_parent"), &RayCast3D::get_exclude_parent);
	ClassDB::bind_method(D_METHOD("set_collide_with_bodies", "enabled"), &RayCast3D::set_collide_with_bodies);
	ClassDB::bind_method(D_METHOD("is_collide_with_bodies_enabled"), &RayCast3D::is_collide_with_bodies_enabled);
	ClassDB::bind_method(D_METHOD("set_collide_with_areas", "enabled"), &RayCast3D::set_collide_with_areas);
	ClassDB::bind_method(D_METHOD("is_collide_with_areas_enabled"), &RayCast3D::is_collide_with_areas_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &RayCast3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &RayCast3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("force_raycast_update"), &RayCast3D::force_raycast_update);
	ClassDB::bind_method(D_METHOD("is_colliding"), &RayCast3D::is_colliding);
	ClassDB::bind_method(D_METHOD("get_collider"), &RayCast3D::get_collider);
	ClassDB::bind_method(D_METHOD("get_collision_point"), &RayCast3D::get_collision_point);
	ClassDB::bind_method(D_METHOD("get_collision_normal"), &RayCast3D::get_collision_normal);

	ADD_GROUP("Ray Cast");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "target_position"), "set_target_position", "get_target_position");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "exclude_parent"), "set_exclude_parent", "get_exclude_parent");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_RANGE, "0,65535,1"), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_bodies"), "set_collide_with_bodies", "is_collide_with_bodies_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_areas"), "set_collide_with_areas", "is_collide_with_areas_enabled");
}
