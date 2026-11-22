#include "node_2d.h"

#include "scene/main/scene_tree.h"

void Node2D::set_position(const Vector2 &p_pos) {
	position = p_pos;
	_update_canvas_transform();
}

void Node2D::set_rotation(real_t p_radians) {
	rotation = p_radians;
	_update_canvas_transform();
}

void Node2D::set_scale(const Vector2 &p_scale) {
	scale = p_scale;
	// Une échelle nulle rend la transformation non inversible.
	if (scale.x == 0) {
		scale.x = (real_t)CMP_EPSILON;
	}
	if (scale.y == 0) {
		scale.y = (real_t)CMP_EPSILON;
	}
	_update_canvas_transform();
}

void Node2D::set_transform(const Transform2D &p_xform) {
	position = p_xform.origin;
	rotation = p_xform.get_rotation();
	scale = p_xform.get_scale();
	_update_canvas_transform();
}

void Node2D::set_global_transform(const Transform2D &p_xform) {
	const CanvasItem *parent_item = Object::cast_to<CanvasItem>(get_parent());
	if (parent_item) {
		set_transform(parent_item->get_global_transform().affine_inverse() * p_xform);
	} else {
		set_transform(p_xform);
	}
}

void Node2D::set_global_position(const Vector2 &p_pos) {
	const CanvasItem *parent_item = Object::cast_to<CanvasItem>(get_parent());
	if (parent_item) {
		set_position(parent_item->get_global_transform().affine_inverse().xform(p_pos));
	} else {
		set_position(p_pos);
	}
}

void Node2D::set_global_rotation(real_t p_radians) {
	const CanvasItem *parent_item = Object::cast_to<CanvasItem>(get_parent());
	if (parent_item) {
		set_rotation(p_radians - parent_item->get_global_transform().get_rotation());
	} else {
		set_rotation(p_radians);
	}
}

void Node2D::look_at(const Vector2 &p_point) {
	rotate(get_angle_to(p_point));
}

real_t Node2D::get_angle_to(const Vector2 &p_point) const {
	return to_local(p_point).operator*(scale).angle();
}

void Node2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_position", "position"), &Node2D::set_position);
	ClassDB::bind_method(D_METHOD("get_position"), &Node2D::get_position);
	ClassDB::bind_method(D_METHOD("set_rotation", "radians"), &Node2D::set_rotation);
	ClassDB::bind_method(D_METHOD("get_rotation"), &Node2D::get_rotation);
	ClassDB::bind_method(D_METHOD("set_rotation_degrees", "degrees"), &Node2D::set_rotation_degrees);
	ClassDB::bind_method(D_METHOD("get_rotation_degrees"), &Node2D::get_rotation_degrees);
	ClassDB::bind_method(D_METHOD("set_scale", "scale"), &Node2D::set_scale);
	ClassDB::bind_method(D_METHOD("get_scale"), &Node2D::get_scale);
	ClassDB::bind_method(D_METHOD("set_global_position", "position"), &Node2D::set_global_position);
	ClassDB::bind_method(D_METHOD("get_global_position"), &Node2D::get_global_position);
	ClassDB::bind_method(D_METHOD("set_global_rotation", "radians"), &Node2D::set_global_rotation);
	ClassDB::bind_method(D_METHOD("get_global_rotation"), &Node2D::get_global_rotation);
	ClassDB::bind_method(D_METHOD("get_global_scale"), &Node2D::get_global_scale);
	ClassDB::bind_method(D_METHOD("translate", "offset"), &Node2D::translate);
	ClassDB::bind_method(D_METHOD("rotate", "radians"), &Node2D::rotate);
	ClassDB::bind_method(D_METHOD("move_local_x", "delta"), &Node2D::move_local_x);
	ClassDB::bind_method(D_METHOD("move_local_y", "delta"), &Node2D::move_local_y);
	ClassDB::bind_method(D_METHOD("look_at", "point"), &Node2D::look_at);
	ClassDB::bind_method(D_METHOD("get_angle_to", "point"), &Node2D::get_angle_to);
	ClassDB::bind_method(D_METHOD("to_local", "global_point"), &Node2D::to_local);
	ClassDB::bind_method(D_METHOD("to_global", "local_point"), &Node2D::to_global);

	ADD_GROUP("Transform");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "position"), "set_position", "get_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation", PROPERTY_HINT_RADIANS), "set_rotation", "get_rotation");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "scale"), "set_scale", "get_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_degrees", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_rotation_degrees", "get_rotation_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "global_position", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_global_position", "get_global_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "global_rotation", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_global_rotation", "get_global_rotation");
}

void Marker2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW && get_tree()->is_edit_mode()) {
		draw_line(Vector2(-10, 0), Vector2(10, 0), Color(1.0f, 0.4f, 0.4f), 1.5f);
		draw_line(Vector2(0, -10), Vector2(0, 10), Color(0.5f, 1.0f, 0.4f), 1.5f);
	}
}
