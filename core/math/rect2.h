#pragma once

#include "core/math/vector2.h"

struct Rect2 {
	Point2 position;
	Size2 size;

	Rect2() {}
	Rect2(real_t x, real_t y, real_t w, real_t h) :
			position(x, y), size(w, h) {}
	Rect2(const Point2 &p_pos, const Size2 &p_size) :
			position(p_pos), size(p_size) {}

	Vector2 get_end() const { return position + size; }
	Vector2 get_center() const { return position + size * 0.5f; }
	real_t get_area() const { return size.x * size.y; }
	bool has_area() const { return size.x > 0 && size.y > 0; }

	bool has_point(const Point2 &p) const {
		return p.x >= position.x && p.y >= position.y && p.x < position.x + size.x && p.y < position.y + size.y;
	}
	bool intersects(const Rect2 &r) const {
		return position.x < r.position.x + r.size.x && position.x + size.x > r.position.x &&
				position.y < r.position.y + r.size.y && position.y + size.y > r.position.y;
	}
	bool encloses(const Rect2 &r) const {
		return r.position.x >= position.x && r.position.y >= position.y &&
				r.position.x + r.size.x <= position.x + size.x && r.position.y + r.size.y <= position.y + size.y;
	}
	Rect2 merge(const Rect2 &r) const {
		Rect2 out;
		out.position.x = std::min(position.x, r.position.x);
		out.position.y = std::min(position.y, r.position.y);
		out.size.x = std::max(position.x + size.x, r.position.x + r.size.x) - out.position.x;
		out.size.y = std::max(position.y + size.y, r.position.y + r.size.y) - out.position.y;
		return out;
	}
	Rect2 intersection(const Rect2 &r) const {
		if (!intersects(r)) {
			return Rect2();
		}
		Rect2 out;
		out.position.x = std::max(position.x, r.position.x);
		out.position.y = std::max(position.y, r.position.y);
		out.size.x = std::min(position.x + size.x, r.position.x + r.size.x) - out.position.x;
		out.size.y = std::min(position.y + size.y, r.position.y + r.size.y) - out.position.y;
		return out;
	}
	Rect2 grow(real_t amount) const {
		return Rect2(position.x - amount, position.y - amount, size.x + amount * 2, size.y + amount * 2);
	}
	Rect2 expand(const Vector2 &p) const {
		Vector2 b = position, e = position + size;
		b.x = std::min(b.x, p.x);
		b.y = std::min(b.y, p.y);
		e.x = std::max(e.x, p.x);
		e.y = std::max(e.y, p.y);
		return Rect2(b, e - b);
	}
	Rect2 abs() const {
		return Rect2(Point2(position.x + std::min(size.x, 0.0f), position.y + std::min(size.y, 0.0f)), size.abs());
	}
	bool operator==(const Rect2 &r) const { return position == r.position && size == r.size; }
	bool operator!=(const Rect2 &r) const { return !(*this == r); }
};
