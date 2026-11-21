#pragma once

#include "core/math/rect2.h"

// Transformation affine 2D : deux colonnes de base (x, y) + origine, comme dans Godot.
struct Transform2D {
	Vector2 x = Vector2(1, 0);
	Vector2 y = Vector2(0, 1);
	Vector2 origin;

	Transform2D() {}
	Transform2D(const Vector2 &p_x, const Vector2 &p_y, const Vector2 &p_origin) :
			x(p_x), y(p_y), origin(p_origin) {}
	Transform2D(real_t p_rot, const Vector2 &p_pos) {
		real_t c = std::cos(p_rot), s = std::sin(p_rot);
		x = Vector2(c, s);
		y = Vector2(-s, c);
		origin = p_pos;
	}
	Transform2D(real_t p_rot, const Size2 &p_scale, const Vector2 &p_pos) {
		real_t c = std::cos(p_rot), s = std::sin(p_rot);
		x = Vector2(c, s) * p_scale.x;
		y = Vector2(-s, c) * p_scale.y;
		origin = p_pos;
	}

	Vector2 basis_xform(const Vector2 &v) const { return Vector2(x.x * v.x + y.x * v.y, x.y * v.x + y.y * v.y); }
	Vector2 xform(const Vector2 &v) const { return basis_xform(v) + origin; }
	Vector2 basis_xform_inv(const Vector2 &v) const { return Vector2(x.dot(v), y.dot(v)); }

	real_t determinant() const { return x.x * y.y - x.y * y.x; }

	Transform2D affine_inverse() const {
		real_t det = determinant();
		Transform2D inv;
		if (det == 0) {
			return inv;
		}
		real_t idet = 1.0f / det;
		inv.x = Vector2(y.y * idet, -x.y * idet);
		inv.y = Vector2(-y.x * idet, x.x * idet);
		inv.origin = inv.basis_xform(-origin);
		return inv;
	}

	Transform2D operator*(const Transform2D &t) const {
		Transform2D out;
		out.x = basis_xform(t.x);
		out.y = basis_xform(t.y);
		out.origin = xform(t.origin);
		return out;
	}

	real_t get_rotation() const { return std::atan2(x.y, x.x); }
	Size2 get_scale() const {
		real_t det_sign = determinant() < 0 ? -1.0f : 1.0f;
		return Size2(x.length(), det_sign * y.length());
	}
	Vector2 get_origin() const { return origin; }

	Transform2D translated(const Vector2 &p_offset) const {
		Transform2D t = *this;
		t.origin += p_offset;
		return t;
	}
	Transform2D scaled(const Size2 &p_scale) const {
		Transform2D t = *this;
		t.x = t.x * p_scale;
		t.y = t.y * p_scale;
		t.origin = t.origin * p_scale;
		return t;
	}
	Transform2D orthonormalized() const {
		Transform2D t;
		t.x = x.normalized();
		t.y = Vector2(-t.x.y, t.x.x);
		t.origin = origin;
		return t;
	}

	Rect2 xform(const Rect2 &r) const {
		Vector2 p[4] = {
			xform(r.position),
			xform(Vector2(r.position.x + r.size.x, r.position.y)),
			xform(Vector2(r.position.x, r.position.y + r.size.y)),
			xform(r.position + r.size),
		};
		Rect2 out(p[0], Size2());
		for (int i = 1; i < 4; i++) {
			out = out.expand(p[i]);
		}
		return out;
	}

	bool operator==(const Transform2D &t) const { return x == t.x && y == t.y && origin == t.origin; }
	bool operator!=(const Transform2D &t) const { return !(*this == t); }
};
