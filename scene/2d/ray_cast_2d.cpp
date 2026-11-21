#include "ray_cast_2d.h"

#include "scene/2d/physics_body_2d.h"
#include "scene/main/scene_tree.h"

void RayCast2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_physics_process_internal(enabled && !get_tree()->is_edit_mode());
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (enabled) {
				bool was_colliding = colliding;
				force_raycast_update();
				if (was_colliding != colliding && get_tree()->is_debugging_collisions_hint()) {
					queue_redraw();
				}
			}
		} break;
		case NOTIFICATION_DRAW: {
			if (!get_tree()->is_edit_mode() && !get_tree()->is_debugging_collisions_hint()) {
				return;
			}
			Color color = colliding ? Color(1.0f, 0.3f, 0.2f, 0.9f) : Color(0.0f, 0.7f, 0.9f, 0.9f);
			if (!enabled) {
				color = Color(0.6f, 0.6f, 0.6f, 0.7f);
			}
			draw_line(Vector2(), target_position, color, 2.0f);
			// Pointe de flèche.
			Vector2 dir = target_position.normalized();
			Vector2 side = Vector2(-dir.y, dir.x);
			std::vector<Vector2> tip = { target_position, target_position - dir * 10 + side * 5, target_position - dir * 10 - side * 5 };
			draw_colored_polygon(tip, color);
		} break;
	}
}

void RayCast2D::set_target_position(const Vector2 &p_target) {
	target_position = p_target;
	queue_redraw();
}

void RayCast2D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (is_inside_tree()) {
		set_physics_process_internal(enabled && !get_tree()->is_edit_mode());
	}
	if (!enabled) {
		colliding = false;
		collider_id = 0;
	}
	queue_redraw();
}

void RayCast2D::force_raycast_update() {
	colliding = false;
	collider_id = 0;
	if (!is_inside_tree() || get_tree()->is_edit_mode()) {
		return;
	}
	const Transform2D global = get_global_transform();
	RID exclude;
	if (exclude_parent) {
		if (CollisionObject2D *co = Object::cast_to<CollisionObject2D>(get_parent())) {
			exclude = co->get_rid();
		}
	}
	PhysicsServer2D::RayResult result;
	if (PhysicsServer2D::get_singleton()->space_intersect_ray(get_tree()->get_space(), global.origin, global.xform(target_position), result, collision_mask, exclude, collide_with_bodies, collide_with_areas)) {
		colliding = true;
		collider_id = result.collider_id;
		collision_point = result.position;
		collision_normal = result.normal;
	}
}

void RayCast2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_position", "target"), &RayCast2D::set_target_position);
	ClassDB::bind_method(D_METHOD("get_target_position"), &RayCast2D::get_target_position);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &RayCast2D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &RayCast2D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_exclude_parent", "exclude"), &RayCast2D::set_exclude_parent);
	ClassDB::bind_method(D_METHOD("get_exclude_parent"), &RayCast2D::get_exclude_parent);
	ClassDB::bind_method(D_METHOD("set_collide_with_bodies", "enabled"), &RayCast2D::set_collide_with_bodies);
	ClassDB::bind_method(D_METHOD("is_collide_with_bodies_enabled"), &RayCast2D::is_collide_with_bodies_enabled);
	ClassDB::bind_method(D_METHOD("set_collide_with_areas", "enabled"), &RayCast2D::set_collide_with_areas);
	ClassDB::bind_method(D_METHOD("is_collide_with_areas_enabled"), &RayCast2D::is_collide_with_areas_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &RayCast2D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &RayCast2D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("force_raycast_update"), &RayCast2D::force_raycast_update);
	ClassDB::bind_method(D_METHOD("is_colliding"), &RayCast2D::is_colliding);
	ClassDB::bind_method(D_METHOD("get_collider"), &RayCast2D::get_collider);
	ClassDB::bind_method(D_METHOD("get_collision_point"), &RayCast2D::get_collision_point);
	ClassDB::bind_method(D_METHOD("get_collision_normal"), &RayCast2D::get_collision_normal);

	ADD_GROUP("Ray Cast");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "target_position"), "set_target_position", "get_target_position");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "exclude_parent"), "set_exclude_parent", "get_exclude_parent");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_RANGE, "0,65535,1"), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_bodies"), "set_collide_with_bodies", "is_collide_with_bodies_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_areas"), "set_collide_with_areas", "is_collide_with_areas_enabled");
}
