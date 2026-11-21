// AABB : boîte englobante alignée sur les axes.
#pragma once

#include "core/math/transform_3d.h"

struct AABB {
	Vector3 position;
	Vector3 size;

	AABB() {}
	AABB(const Vector3 &p_position, const Vector3 &p_size) :
			position(p_position), size(p_size) {}

	Vector3 get_end() const { return position + size; }
	Vector3 get_center() const { return position + size * 0.5f; }
	bool has_volume() const { return size.x > 0 && size.y > 0 && size.z > 0; }
	bool intersects(const AABB &b) const {
		return position.x <= b.position.x + b.size.x && b.position.x <= position.x + size.x &&
				position.y <= b.position.y + b.size.y && b.position.y <= position.y + size.y &&
				position.z <= b.position.z + b.size.z && b.position.z <= position.z + size.z;
	}
	void expand_to(const Vector3 &p) {
		const Vector3 end = get_end();
		const Vector3 lo(std::min(position.x, p.x), std::min(position.y, p.y), std::min(position.z, p.z));
		const Vector3 hi(std::max(end.x, p.x), std::max(end.y, p.y), std::max(end.z, p.z));
		position = lo;
		size = hi - lo;
	}
	AABB merge(const AABB &b) const {
		AABB r = *this;
		r.expand_to(b.position);
		r.expand_to(b.get_end());
		return r;
	}
	AABB grow(real_t p_by) const { return AABB(position - Vector3(p_by, p_by, p_by), size + Vector3(p_by, p_by, p_by) * 2.0f); }
	Vector3 get_corner(int i) const {
		return Vector3(position.x + ((i & 1) ? size.x : 0), position.y + ((i & 2) ? size.y : 0), position.z + ((i & 4) ? size.z : 0));
	}
	// Boîte englobant cette boîte après transformation.
	AABB transformed(const Transform3D &t) const {
		AABB r(t.xform(get_corner(0)), Vector3());
		for (int i = 1; i < 8; i++) {
			r.expand_to(t.xform(get_corner(i)));
		}
		return r;
	}
	// Intersection avec le rayon origine + t * direction (t >= 0). Retourne t dans r_t.
	bool intersects_ray(const Vector3 &p_from, const Vector3 &p_dir, real_t *r_t = nullptr) const {
		real_t tmin = 0, tmax = 1e30f;
		const Vector3 end = get_end();
		for (int i = 0; i < 3; i++) {
			if (std::fabs(p_dir[i]) < 1e-12f) {
				if (p_from[i] < position[i] || p_from[i] > end[i]) {
					return false;
				}
				continue;
			}
			real_t t1 = (position[i] - p_from[i]) / p_dir[i];
			real_t t2 = (end[i] - p_from[i]) / p_dir[i];
			if (t1 > t2) {
				std::swap(t1, t2);
			}
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax) {
				return false;
			}
		}
		if (r_t) {
			*r_t = tmin;
		}
		return true;
	}
};
