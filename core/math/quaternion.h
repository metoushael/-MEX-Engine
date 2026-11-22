// Quaternion : rotation dans l'espace, interpolable sans blocage de cardan.
//
// Porté de core/math/quaternion.{h,cpp} et de Basis::set_quaternion du moteur Godot 4.3.
// Copyright (c) 2014-present Godot Engine contributors, (c) 2007-2014 Juan Linietsky, Ariel Manzur.
// Licence MIT : thirdparty/godot/LICENSE.txt.
#pragma once

#include "core/math/basis.h"

struct Quaternion {
	real_t x = 0;
	real_t y = 0;
	real_t z = 0;
	real_t w = 1;

	Quaternion() {}
	Quaternion(real_t p_x, real_t p_y, real_t p_z, real_t p_w) :
			x(p_x), y(p_y), z(p_z), w(p_w) {}
	// Rotation de p_angle radians autour de p_axis (axe normalisé).
	Quaternion(const Vector3 &p_axis, real_t p_angle) {
		const real_t d = p_axis.length();
		if (d == 0) {
			x = y = z = w = 0;
		} else {
			const real_t sin_angle = std::sin(p_angle * 0.5f);
			const real_t cos_angle = std::cos(p_angle * 0.5f);
			const real_t s = sin_angle / d;
			x = p_axis.x * s;
			y = p_axis.y * s;
			z = p_axis.z * s;
			w = cos_angle;
		}
	}

	// Depuis une matrice de rotation orthonormée (Basis::get_quaternion de Godot).
	explicit Quaternion(const Basis &p_basis) {
		const auto m = [&](int i, int j) { return p_basis.get_column(j)[i]; };
		const real_t trace = m(0, 0) + m(1, 1) + m(2, 2);
		real_t temp[4];
		if (trace > 0.0f) {
			real_t s = std::sqrt(trace + 1.0f);
			temp[3] = (s * 0.5f);
			s = 0.5f / s;
			temp[0] = ((m(2, 1) - m(1, 2)) * s);
			temp[1] = ((m(0, 2) - m(2, 0)) * s);
			temp[2] = ((m(1, 0) - m(0, 1)) * s);
		} else {
			const int i = m(0, 0) < m(1, 1) ? (m(1, 1) < m(2, 2) ? 2 : 1) : (m(0, 0) < m(2, 2) ? 2 : 0);
			const int j = (i + 1) % 3;
			const int k = (i + 2) % 3;
			real_t s = std::sqrt(m(i, i) - m(j, j) - m(k, k) + 1.0f);
			temp[i] = s * 0.5f;
			s = 0.5f / s;
			temp[3] = (m(k, j) - m(j, k)) * s;
			temp[j] = (m(j, i) + m(i, j)) * s;
			temp[k] = (m(k, i) + m(i, k)) * s;
		}
		x = temp[0];
		y = temp[1];
		z = temp[2];
		w = temp[3];
	}

	// Angles d'Euler en radians, ordre YXZ (celui de Node3D.rotation).
	static Quaternion from_euler(const Vector3 &p_euler) {
		const real_t half_a1 = p_euler.y * 0.5f;
		const real_t half_a2 = p_euler.x * 0.5f;
		const real_t half_a3 = p_euler.z * 0.5f;
		// R = Y(a1).X(a2).Z(a3)
		const real_t cos_a1 = std::cos(half_a1), sin_a1 = std::sin(half_a1);
		const real_t cos_a2 = std::cos(half_a2), sin_a2 = std::sin(half_a2);
		const real_t cos_a3 = std::cos(half_a3), sin_a3 = std::sin(half_a3);
		return Quaternion(
				sin_a1 * cos_a2 * sin_a3 + cos_a1 * sin_a2 * cos_a3,
				sin_a1 * cos_a2 * cos_a3 - cos_a1 * sin_a2 * sin_a3,
				-sin_a1 * sin_a2 * cos_a3 + cos_a1 * cos_a2 * sin_a3,
				sin_a1 * sin_a2 * sin_a3 + cos_a1 * cos_a2 * cos_a3);
	}

	real_t dot(const Quaternion &p_q) const { return x * p_q.x + y * p_q.y + z * p_q.z + w * p_q.w; }
	real_t length_squared() const { return dot(*this); }
	real_t length() const { return std::sqrt(length_squared()); }
	Quaternion normalized() const {
		const real_t l = length();
		return l > 0 ? Quaternion(x / l, y / l, z / l, w / l) : Quaternion();
	}
	Quaternion inverse() const { return Quaternion(-x, -y, -z, w); } // quaternion unitaire
	Quaternion operator-() const { return Quaternion(-x, -y, -z, -w); }
	Quaternion operator*(const Quaternion &p_q) const {
		return Quaternion(
				w * p_q.x + x * p_q.w + y * p_q.z - z * p_q.y,
				w * p_q.y + y * p_q.w + z * p_q.x - x * p_q.z,
				w * p_q.z + z * p_q.w + x * p_q.y - y * p_q.x,
				w * p_q.w - x * p_q.x - y * p_q.y - z * p_q.z);
	}
	Vector3 xform(const Vector3 &p_v) const {
		const Vector3 u(x, y, z);
		const Vector3 uv = u.cross(p_v);
		return p_v + ((uv * w) + u.cross(uv)) * (real_t)2;
	}

	// Interpolation sphérique, par le chemin le plus court.
	Quaternion slerp(const Quaternion &p_to, real_t p_weight) const {
		Quaternion to1;
		real_t omega, cosom, sinom, scale0, scale1;
		// calc cosine
		cosom = dot(p_to);
		// adjust signs (if necessary)
		if (cosom < 0.0f) {
			cosom = -cosom;
			to1 = -p_to;
		} else {
			to1 = p_to;
		}
		// calculate coefficients
		if ((1.0f - cosom) > (real_t)0.00001) {
			// standard case (slerp)
			omega = std::acos(cosom);
			sinom = std::sin(omega);
			scale0 = std::sin((1.0f - p_weight) * omega) / sinom;
			scale1 = std::sin(p_weight * omega) / sinom;
		} else {
			// "from" and "to" quaternions are very close
			//  ... so we can do a linear interpolation
			scale0 = 1.0f - p_weight;
			scale1 = p_weight;
		}
		// calculate final values
		return Quaternion(
				scale0 * x + scale1 * to1.x,
				scale0 * y + scale1 * to1.y,
				scale0 * z + scale1 * to1.z,
				scale0 * w + scale1 * to1.w);
	}

	// Matrice de rotation (Basis::set_quaternion de Godot).
	Basis to_basis() const {
		const real_t d = length_squared();
		if (d == 0) {
			return Basis();
		}
		const real_t s = 2.0f / d;
		const real_t xs = x * s, ys = y * s, zs = z * s;
		const real_t wx = w * xs, wy = w * ys, wz = w * zs;
		const real_t xx = x * xs, xy = x * ys, xz = x * zs;
		const real_t yy = y * ys, yz = y * zs, zz = z * zs;
		// Colonnes de la matrice.
		return Basis(
				Vector3(1.0f - (yy + zz), xy + wz, xz - wy),
				Vector3(xy - wz, 1.0f - (xx + zz), yz + wx),
				Vector3(xz + wy, yz - wx, 1.0f - (xx + yy)));
	}
	Vector3 get_euler() const { return to_basis().get_euler(); }
};
