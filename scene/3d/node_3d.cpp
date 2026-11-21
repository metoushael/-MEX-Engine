#include "node_3d.h"

#include <cmath>

void Node3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_PARENTED: {
			global_dirty = true;
		} break;
	}
}

void Node3D::_propagate_transform_changed() {
	global_dirty = true;
	notification(NOTIFICATION_TRANSFORM_CHANGED);
	// Copie : une notification peut modifier la liste des enfants.
	const std::vector<Node *> children = get_children_vector();
	for (Node *child : children) {
		if (Node3D *n = Object::cast_to<Node3D>(child)) {
			n->_propagate_transform_changed();
		}
	}
}

void Node3D::_propagate_visibility_changed() {
	notification(NOTIFICATION_VISIBILITY_CHANGED_3D);
	const std::vector<Node *> children = get_children_vector();
	for (Node *child : children) {
		Node3D *n = Object::cast_to<Node3D>(child);
		if (n && n->visible) {
			n->_propagate_visibility_changed();
		}
	}
}

void Node3D::set_position(const Vector3 &p_position) {
	if (position == p_position) {
		return;
	}
	position = p_position;
	_propagate_transform_changed();
}

void Node3D::set_rotation(const Vector3 &p_radians) {
	if (rotation == p_radians) {
		return;
	}
	rotation = p_radians;
	_propagate_transform_changed();
}

void Node3D::set_rotation_degrees(const Vector3 &p_degrees) {
	set_rotation(p_degrees * (real_t)(Math_PI / 180.0));
}

Vector3 Node3D::get_rotation_degrees() const {
	return rotation * (real_t)(180.0 / Math_PI);
}

void Node3D::set_scale(const Vector3 &p_scale) {
	if (scale == p_scale) {
		return;
	}
	scale = p_scale;
	_propagate_transform_changed();
}

Transform3D Node3D::get_transform() const {
	return Transform3D(Basis::from_euler(rotation).scaled_local(scale), position);
}

void Node3D::set_transform(const Transform3D &p_transform) {
	Vector3 s = p_transform.basis.get_scale();
	// Une base indirecte (miroir) est représentée par une échelle X négative.
	if (p_transform.basis.determinant() < 0) {
		s.x = -s.x;
	}
	Basis rot = p_transform.basis;
	for (int i = 0; i < 3; i++) {
		if (std::fabs(s[i]) > 1e-9f) {
			rot.set_column(i, rot.get_column(i) / s[i]);
		}
	}
	position = p_transform.origin;
	rotation = rot.orthonormalized().get_euler();
	scale = s;
	_propagate_transform_changed();
}

Transform3D Node3D::get_global_transform() const {
	if (global_dirty) {
		const Node3D *parent = Object::cast_to<Node3D>(get_parent());
		global_cache = parent ? parent->get_global_transform() * get_transform() : get_transform();
		global_dirty = false;
	}
	return global_cache;
}

void Node3D::set_global_transform(const Transform3D &p_transform) {
	const Node3D *parent = Object::cast_to<Node3D>(get_parent());
	set_transform(parent ? parent->get_global_transform().affine_inverse() * p_transform : p_transform);
}

void Node3D::set_global_position(const Vector3 &p_position) {
	const Node3D *parent = Object::cast_to<Node3D>(get_parent());
	set_position(parent ? parent->get_global_transform().affine_inverse().xform(p_position) : p_position);
}

void Node3D::set_global_rotation(const Vector3 &p_radians) {
	Transform3D global = get_global_transform();
	const Vector3 s = global.basis.get_scale();
	global.basis = Basis::from_euler(p_radians).scaled_local(s);
	set_global_transform(global);
}

Vector3 Node3D::get_global_rotation() const {
	return get_global_transform().basis.orthonormalized().get_euler();
}

void Node3D::translate(const Vector3 &p_offset) {
	set_position(position + Basis::from_euler(rotation).xform(p_offset));
}

void Node3D::global_translate(const Vector3 &p_offset) {
	set_global_position(get_global_position() + p_offset);
}

void Node3D::rotate(const Vector3 &p_axis, real_t p_angle) {
	const Vector3 axis = p_axis.normalized();
	if (axis.is_zero_approx()) {
		return;
	}
	set_rotation((Basis(axis, p_angle) * Basis::from_euler(rotation)).orthonormalized().get_euler());
}

void Node3D::rotate_object_local(const Vector3 &p_axis, real_t p_angle) {
	const Vector3 axis = p_axis.normalized();
	if (axis.is_zero_approx()) {
		return;
	}
	set_rotation((Basis::from_euler(rotation) * Basis(axis, p_angle)).orthonormalized().get_euler());
}

void Node3D::look_at(const Vector3 &p_target, const Vector3 &p_up) {
	const Transform3D global = get_global_transform();
	if ((p_target - global.origin).length_squared() < 1e-10f) {
		return;
	}
	const Vector3 s = global.basis.get_scale();
	Transform3D looking = global.looking_at(p_target, p_up);
	looking.basis = looking.basis.scaled_local(s);
	set_global_transform(looking);
}

void Node3D::set_visible(bool p_visible) {
	if (visible == p_visible) {
		return;
	}
	visible = p_visible;
	_propagate_visibility_changed();
}

bool Node3D::is_visible_in_tree() const {
	const Node3D *n = this;
	while (n) {
		if (!n->visible) {
			return false;
		}
		n = Object::cast_to<Node3D>(n->get_parent());
	}
	return true;
}

void Node3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_position", "position"), &Node3D::set_position);
	ClassDB::bind_method(D_METHOD("get_position"), &Node3D::get_position);
	ClassDB::bind_method(D_METHOD("set_rotation", "radians"), &Node3D::set_rotation);
	ClassDB::bind_method(D_METHOD("get_rotation"), &Node3D::get_rotation);
	ClassDB::bind_method(D_METHOD("set_rotation_degrees", "degrees"), &Node3D::set_rotation_degrees);
	ClassDB::bind_method(D_METHOD("get_rotation_degrees"), &Node3D::get_rotation_degrees);
	ClassDB::bind_method(D_METHOD("set_scale", "scale"), &Node3D::set_scale);
	ClassDB::bind_method(D_METHOD("get_scale"), &Node3D::get_scale);
	ClassDB::bind_method(D_METHOD("set_global_position", "position"), &Node3D::set_global_position);
	ClassDB::bind_method(D_METHOD("get_global_position"), &Node3D::get_global_position);
	ClassDB::bind_method(D_METHOD("set_global_rotation", "radians"), &Node3D::set_global_rotation);
	ClassDB::bind_method(D_METHOD("get_global_rotation"), &Node3D::get_global_rotation);
	ClassDB::bind_method(D_METHOD("translate", "offset"), &Node3D::translate);
	ClassDB::bind_method(D_METHOD("global_translate", "offset"), &Node3D::global_translate);
	ClassDB::bind_method(D_METHOD("rotate", "axis", "angle"), &Node3D::rotate);
	ClassDB::bind_method(D_METHOD("rotate_x", "angle"), &Node3D::rotate_x);
	ClassDB::bind_method(D_METHOD("rotate_y", "angle"), &Node3D::rotate_y);
	ClassDB::bind_method(D_METHOD("rotate_z", "angle"), &Node3D::rotate_z);
	ClassDB::bind_method(D_METHOD("rotate_object_local", "axis", "angle"), &Node3D::rotate_object_local);
	ClassDB::bind_method(D_METHOD("look_at", "target", "up"), &Node3D::look_at, DEFVAL(Vector3(0, 1, 0)));
	ClassDB::bind_method(D_METHOD("to_local", "global_point"), &Node3D::to_local);
	ClassDB::bind_method(D_METHOD("to_global", "local_point"), &Node3D::to_global);
	ClassDB::bind_method(D_METHOD("get_forward"), &Node3D::get_forward);
	ClassDB::bind_method(D_METHOD("get_right"), &Node3D::get_right);
	ClassDB::bind_method(D_METHOD("get_up"), &Node3D::get_up);
	ClassDB::bind_method(D_METHOD("set_visible", "visible"), &Node3D::set_visible);
	ClassDB::bind_method(D_METHOD("is_visible"), &Node3D::is_visible);
	ClassDB::bind_method(D_METHOD("is_visible_in_tree"), &Node3D::is_visible_in_tree);
	ClassDB::bind_method(D_METHOD("show"), &Node3D::show);
	ClassDB::bind_method(D_METHOD("hide"), &Node3D::hide);

	ADD_GROUP("Transform");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "position"), "set_position", "get_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "rotation", PROPERTY_HINT_RADIANS), "set_rotation", "get_rotation");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "rotation_degrees", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_rotation_degrees", "get_rotation_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "scale"), "set_scale", "get_scale");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "global_position", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_global_position", "get_global_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "global_rotation", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_global_rotation", "get_global_rotation");
	ADD_GROUP("Visibility");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "visible"), "set_visible", "is_visible");
}

void Marker3D::_edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const {
	const Transform3D t = get_global_transform().orthonormalized();
	const Color colors[3] = { Color(0.92f, 0.32f, 0.32f, 1), Color(0.45f, 0.85f, 0.32f, 1), Color(0.32f, 0.55f, 0.96f, 1) };
	for (int i = 0; i < 3; i++) {
		const Vector3 axis = t.basis.get_column(i) * 0.3f;
		r_lines->push_back({ t.origin - axis, t.origin + axis, colors[i] });
	}
}
