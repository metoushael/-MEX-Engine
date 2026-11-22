#include "shape_3d.h"

namespace DebugLines3D {

void add_box(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const AABB &p_box, const Color &p_color) {
	Vector3 c[8];
	for (int i = 0; i < 8; i++) {
		c[i] = p_xform.xform(p_box.get_corner(i));
	}
	// Les coins sont numérotés par bits (x = 1, y = 2, z = 4) : une arête relie deux coins
	// qui ne diffèrent que d'un bit.
	for (int i = 0; i < 8; i++) {
		for (int bit = 1; bit <= 4; bit <<= 1) {
			if (!(i & bit)) {
				r_lines->push_back({ c[i], c[i | bit], p_color });
			}
		}
	}
}

void add_arc(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Vector3 &p_center, const Vector3 &p_axis_u, const Vector3 &p_axis_v, real_t p_radius, real_t p_from, real_t p_to, const Color &p_color, int p_segments) {
	Vector3 previous;
	for (int i = 0; i <= p_segments; i++) {
		const real_t a = p_from + (p_to - p_from) * (real_t)i / (real_t)p_segments;
		const Vector3 p = p_xform.xform(p_center + (p_axis_u * std::cos(a) + p_axis_v * std::sin(a)) * p_radius);
		if (i > 0) {
			r_lines->push_back({ previous, p, p_color });
		}
		previous = p;
	}
}

} // namespace DebugLines3D

// --- SphereShape3D ---
void SphereShape3D::set_radius(real_t p_radius) {
	radius = std::max(p_radius, 0.001f);
	_shape_changed();
}

PhysicsServer3D::ShapeData SphereShape3D::get_physics_shape() const {
	PhysicsServer3D::ShapeData sd;
	sd.type = PhysicsServer3D::SHAPE_SPHERE;
	sd.radius = radius;
	return sd;
}

void SphereShape3D::get_debug_lines(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Color &p_color) const {
	const real_t tau = (real_t)Math_TAU;
	DebugLines3D::add_arc(r_lines, p_xform, Vector3(), Vector3(1, 0, 0), Vector3(0, 1, 0), radius, 0, tau, p_color);
	DebugLines3D::add_arc(r_lines, p_xform, Vector3(), Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 0, tau, p_color);
	DebugLines3D::add_arc(r_lines, p_xform, Vector3(), Vector3(0, 1, 0), Vector3(0, 0, 1), radius, 0, tau, p_color);
}

void SphereShape3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &SphereShape3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &SphereShape3D::get_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_radius", "get_radius");
}

// --- BoxShape3D ---
void BoxShape3D::set_size(const Vector3 &p_size) {
	size = Vector3(std::max(p_size.x, 0.001f), std::max(p_size.y, 0.001f), std::max(p_size.z, 0.001f));
	_shape_changed();
}

PhysicsServer3D::ShapeData BoxShape3D::get_physics_shape() const {
	PhysicsServer3D::ShapeData sd;
	sd.type = PhysicsServer3D::SHAPE_BOX;
	sd.half_extents = size * 0.5f;
	return sd;
}

void BoxShape3D::get_debug_lines(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Color &p_color) const {
	DebugLines3D::add_box(r_lines, p_xform, get_aabb(), p_color);
}

void BoxShape3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &BoxShape3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &BoxShape3D::get_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size"), "set_size", "get_size");
}

// --- CapsuleShape3D ---
void CapsuleShape3D::set_radius(real_t p_radius) {
	radius = std::max(p_radius, 0.001f);
	if (height < radius * 2) {
		height = radius * 2;
	}
	_shape_changed();
}

void CapsuleShape3D::set_height(real_t p_height) {
	height = std::max(p_height, radius * 2);
	_shape_changed();
}

PhysicsServer3D::ShapeData CapsuleShape3D::get_physics_shape() const {
	PhysicsServer3D::ShapeData sd;
	sd.type = PhysicsServer3D::SHAPE_CAPSULE;
	sd.radius = radius;
	sd.height = height;
	return sd;
}

void CapsuleShape3D::get_debug_lines(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Color &p_color) const {
	const real_t half = std::max(height * 0.5f - radius, 0.0f);
	const real_t pi = (real_t)Math_PI;
	const Vector3 top(0, half, 0), bottom(0, -half, 0);
	const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
	DebugLines3D::add_arc(r_lines, p_xform, top, x, z, radius, 0, pi * 2, p_color);
	DebugLines3D::add_arc(r_lines, p_xform, bottom, x, z, radius, 0, pi * 2, p_color);
	DebugLines3D::add_arc(r_lines, p_xform, top, x, y, radius, 0, pi, p_color, 16);
	DebugLines3D::add_arc(r_lines, p_xform, top, z, y, radius, 0, pi, p_color, 16);
	DebugLines3D::add_arc(r_lines, p_xform, bottom, x, y, radius, pi, pi * 2, p_color, 16);
	DebugLines3D::add_arc(r_lines, p_xform, bottom, z, y, radius, pi, pi * 2, p_color, 16);
	const Vector3 sides[4] = { x, -x, z, -z };
	for (const Vector3 &s : sides) {
		r_lines->push_back({ p_xform.xform(top + s * radius), p_xform.xform(bottom + s * radius), p_color });
	}
}

void CapsuleShape3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &CapsuleShape3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &CapsuleShape3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &CapsuleShape3D::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &CapsuleShape3D::get_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_height", "get_height");
}
