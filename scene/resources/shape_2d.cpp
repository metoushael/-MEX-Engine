#include "shape_2d.h"

#include "scene/main/canvas_item.h"

static Color _outline_color(const Color &p_color) {
	return Color(p_color.r, p_color.g, p_color.b, std::min(1.0f, p_color.a * 2.5f));
}

// --- RectangleShape2D ---
void RectangleShape2D::set_size(const Vector2 &p_size) {
	size = Vector2(std::max(p_size.x, 0.01f), std::max(p_size.y, 0.01f));
	_shape_changed();
}

void RectangleShape2D::get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const {
	PhysicsServer2D::ShapeData sd;
	sd.type = PhysicsServer2D::SHAPE_POLYGON;
	const Vector2 h = size * 0.5f;
	sd.points = { Vector2(-h.x, -h.y), Vector2(h.x, -h.y), Vector2(h.x, h.y), Vector2(-h.x, h.y) };
	r_shapes->push_back(sd);
}

void RectangleShape2D::draw(CanvasItem *p_item, const Color &p_color) const {
	p_item->draw_rect(get_rect(), p_color, true);
	p_item->draw_rect(get_rect(), _outline_color(p_color), false, 1.0f);
}

void RectangleShape2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &RectangleShape2D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &RectangleShape2D::get_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size"), "set_size", "get_size");
}

// --- CircleShape2D ---
void CircleShape2D::set_radius(real_t p_radius) {
	radius = std::max(p_radius, 0.01f);
	_shape_changed();
}

void CircleShape2D::get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const {
	PhysicsServer2D::ShapeData sd;
	sd.type = PhysicsServer2D::SHAPE_CIRCLE;
	sd.radius = radius;
	r_shapes->push_back(sd);
}

void CircleShape2D::draw(CanvasItem *p_item, const Color &p_color) const {
	p_item->draw_circle(Vector2(), radius, p_color);
	p_item->draw_arc(Vector2(), radius, 0, (real_t)Math_TAU, 33, _outline_color(p_color), 1.0f);
}

void CircleShape2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &CircleShape2D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &CircleShape2D::get_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,4096,0.01"), "set_radius", "get_radius");
}

// --- CapsuleShape2D ---
void CapsuleShape2D::set_radius(real_t p_radius) {
	radius = std::max(p_radius, 0.01f);
	if (height < radius * 2) {
		height = radius * 2;
	}
	_shape_changed();
}

void CapsuleShape2D::set_height(real_t p_height) {
	height = std::max(p_height, radius * 2);
	_shape_changed();
}

void CapsuleShape2D::get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const {
	const real_t half = std::max(0.0f, height * 0.5f - radius);
	PhysicsServer2D::ShapeData top;
	top.type = PhysicsServer2D::SHAPE_CIRCLE;
	top.radius = radius;
	top.center = Vector2(0, -half);
	r_shapes->push_back(top);
	if (half > 0) {
		PhysicsServer2D::ShapeData bottom = top;
		bottom.center = Vector2(0, half);
		r_shapes->push_back(bottom);
		PhysicsServer2D::ShapeData mid;
		mid.type = PhysicsServer2D::SHAPE_POLYGON;
		mid.points = { Vector2(-radius, -half), Vector2(radius, -half), Vector2(radius, half), Vector2(-radius, half) };
		r_shapes->push_back(mid);
	}
}

void CapsuleShape2D::draw(CanvasItem *p_item, const Color &p_color) const {
	const real_t half = std::max(0.0f, height * 0.5f - radius);
	std::vector<Vector2> pts;
	const int segments = 12;
	for (int i = 0; i <= segments; i++) {
		real_t a = (real_t)Math_PI + (real_t)Math_PI * (real_t)i / (real_t)segments;
		pts.push_back(Vector2(0, -half) + Vector2::from_angle(a) * radius);
	}
	for (int i = 0; i <= segments; i++) {
		real_t a = (real_t)Math_PI * (real_t)i / (real_t)segments;
		pts.push_back(Vector2(0, half) + Vector2::from_angle(a) * radius);
	}
	p_item->draw_colored_polygon(pts, p_color);
	p_item->draw_polyline(pts, _outline_color(p_color), 1.0f, true);
}

void CapsuleShape2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &CapsuleShape2D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &CapsuleShape2D::get_radius);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &CapsuleShape2D::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &CapsuleShape2D::get_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,4096,0.01"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.01,4096,0.01"), "set_height", "get_height");
}

// --- ConvexPolygonShape2D ---
ConvexPolygonShape2D::ConvexPolygonShape2D() {
	points = { Vector2(0, -24), Vector2(24, 16), Vector2(-24, 16) };
}

void ConvexPolygonShape2D::set_points(const Array &p_points) {
	points.clear();
	for (const Variant &v : p_points.vec()) {
		points.push_back(v.as_vector2());
	}
	_shape_changed();
}

Array ConvexPolygonShape2D::get_points() const {
	Array out;
	for (const Vector2 &p : points) {
		out.push_back(Variant(p));
	}
	return out;
}

void ConvexPolygonShape2D::get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const {
	if (points.size() < 3) {
		return;
	}
	PhysicsServer2D::ShapeData sd;
	sd.type = PhysicsServer2D::SHAPE_POLYGON;
	sd.points = points;
	r_shapes->push_back(sd);
}

void ConvexPolygonShape2D::draw(CanvasItem *p_item, const Color &p_color) const {
	if (points.size() < 3) {
		return;
	}
	p_item->draw_colored_polygon(points, p_color);
	p_item->draw_polyline(points, _outline_color(p_color), 1.0f, true);
}

Rect2 ConvexPolygonShape2D::get_rect() const {
	if (points.empty()) {
		return Rect2();
	}
	Rect2 r(points[0], Size2());
	for (const Vector2 &p : points) {
		r = r.expand(p);
	}
	return r;
}

void ConvexPolygonShape2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_points", "points"), &ConvexPolygonShape2D::set_points);
	ClassDB::bind_method(D_METHOD("get_points"), &ConvexPolygonShape2D::get_points);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "points"), "set_points", "get_points");
}
