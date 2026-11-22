#include "collision_shape_2d.h"

#include "scene/main/scene_tree.h"

void CollisionShape2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAW: {
			if (shape.is_null()) {
				return;
			}
			if (!get_tree()->is_edit_mode() && !get_tree()->is_debugging_collisions_hint()) {
				return;
			}
			Color color = debug_color;
			if (disabled) {
				color = Color(0.6f, 0.6f, 0.6f, debug_color.a * 0.6f);
			}
			shape->draw(this, color);
		} break;
	}
}

void CollisionShape2D::_shape_changed() {
	queue_redraw();
}

void CollisionShape2D::set_shape(const Ref<Shape2D> &p_shape) {
	if (shape == p_shape) {
		return;
	}
	if (shape.is_valid()) {
		shape->disconnect("changed", Callable(this, "_shape_changed"));
	}
	shape = p_shape;
	if (shape.is_valid()) {
		shape->connect("changed", Callable(this, "_shape_changed"));
	}
	queue_redraw();
}

void CollisionShape2D::set_disabled(bool p_disabled) {
	disabled = p_disabled;
	queue_redraw();
}

void CollisionShape2D::set_debug_color(const Color &p_color) {
	debug_color = p_color;
	queue_redraw();
}

void CollisionShape2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shape", "shape"), &CollisionShape2D::set_shape);
	ClassDB::bind_method(D_METHOD("get_shape"), &CollisionShape2D::get_shape);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &CollisionShape2D::set_disabled);
	ClassDB::bind_method(D_METHOD("is_disabled"), &CollisionShape2D::is_disabled);
	ClassDB::bind_method(D_METHOD("set_debug_color", "color"), &CollisionShape2D::set_debug_color);
	ClassDB::bind_method(D_METHOD("get_debug_color"), &CollisionShape2D::get_debug_color);
	ClassDB::bind_method(D_METHOD("_shape_changed"), &CollisionShape2D::_shape_changed);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shape", PROPERTY_HINT_RESOURCE_TYPE, "Shape2D"), "set_shape", "get_shape");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "disabled"), "set_disabled", "is_disabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "debug_color"), "set_debug_color", "get_debug_color");
}
