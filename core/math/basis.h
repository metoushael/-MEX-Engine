// Basis : matrice 3x3 (rotation et échelle). Les colonnes sont les axes X, Y et Z.
// Repère droit, Y vers le haut, comme dans Godot ; angles d'Euler dans l'ordre YXZ.
#pragma once

#include "core/math/vector3.h"

struct Basis {
	Vector3 rows[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };

	Basis() {}
	Basis(const Vector3 &p_x_axis, const Vector3 &p_y_axis, const Vector3 &p_z_axis) {
		set_column(0, p_x_axis);
		set_column(1, p_y_axis);
		set_column(2, p_z_axis);
	}
	// Rotation d'un angle autour d'un axe unitaire.
	Basis(const Vector3 &p_axis, real_t p_angle) {
		const real_t c = std::cos(p_angle), s = std::sin(p_angle), t = 1 - c;
		const real_t x = p_axis.x, y = p_axis.y, z = p_axis.z;
		rows[0] = Vector3(t * x * x + c, t * x * y - s * z, t * x * z + s * y);
		rows[1] = Vector3(t * x * y + s * z, t * y * y + c, t * y * z - s * x);
		rows[2] = Vector3(t * x * z - s * y, t * y * z + s * x, t * z * z + c);
	}

	Vector3 get_column(int i) const { return Vector3(rows[0][i], rows[1][i], rows[2][i]); }
	void set_column(int i, const Vector3 &v) {
		rows[0][i] = v.x;
		rows[1][i] = v.y;
		rows[2][i] = v.z;
	}

	Vector3 xform(const Vector3 &v) const { return Vector3(rows[0].dot(v), rows[1].dot(v), rows[2].dot(v)); }
	// Produit par la transposée (l'inverse d'une rotation pure).
	Vector3 xform_inv(const Vector3 &v) const {
		return Vector3(rows[0].x * v.x + rows[1].x * v.y + rows[2].x * v.z,
				rows[0].y * v.x + rows[1].y * v.y + rows[2].y * v.z,
				rows[0].z * v.x + rows[1].z * v.y + rows[2].z * v.z);
	}
	Basis operator*(const Basis &b) const {
		Basis r;
		for (int i = 0; i < 3; i++) {
			r.rows[i] = Vector3(rows[i].dot(b.get_column(0)), rows[i].dot(b.get_column(1)), rows[i].dot(b.get_column(2)));
		}
		return r;
	}
	bool operator==(const Basis &b) const { return rows[0] == b.rows[0] && rows[1] == b.rows[1] && rows[2] == b.rows[2]; }
	bool operator!=(const Basis &b) const { return !(*this == b); }

	Basis transposed() const { return Basis(rows[0], rows[1], rows[2]); }
	real_t determinant() const { return rows[0].dot(rows[1].cross(rows[2])); }
	Basis inverse() const {
		const Vector3 c0 = rows[1].cross(rows[2]), c1 = rows[2].cross(rows[0]), c2 = rows[0].cross(rows[1]);
		const real_t det = rows[0].dot(c0);
		if (std::fabs(det) < 1e-12f) {
			return Basis();
		}
		// L'inverse a pour colonnes c0, c1, c2 divisés par le déterminant.
		return Basis(c0 / det, c1 / det, c2 / det);
	}
	Basis orthonormalized() const {
		Vector3 x = get_column(0).normalized();
		Vector3 y = get_column(1);
		y = (y - x * x.dot(y)).normalized();
		Vector3 z = x.cross(y);
		return Basis(x, y, z);
	}
	Vector3 get_scale() const { return Vector3(get_column(0).length(), get_column(1).length(), get_column(2).length()); }
	// Échelle appliquée dans le repère local (colonnes multipliées).
	Basis scaled_local(const Vector3 &s) const { return Basis(get_column(0) * s.x, get_column(1) * s.y, get_column(2) * s.z); }

	// Angles d'Euler (radians), ordre YXZ : R = Ry(y) * Rx(x) * Rz(z).
	static Basis from_euler(const Vector3 &e) {
		const real_t cx = std::cos(e.x), sx = std::sin(e.x);
		const real_t cy = std::cos(e.y), sy = std::sin(e.y);
		const real_t cz = std::cos(e.z), sz = std::sin(e.z);
		Basis b;
		b.rows[0] = Vector3(cy * cz + sy * sx * sz, cz * sy * sx - cy * sz, cx * sy);
		b.rows[1] = Vector3(cx * sz, cx * cz, -sx);
		b.rows[2] = Vector3(cy * sx * sz - cz * sy, cy * cz * sx + sy * sz, cy * cx);
		return b;
	}
	// Suppose une base orthonormée (sans échelle).
	Vector3 get_euler() const {
		Vector3 e;
		const real_t m12 = rows[1].z;
		if (m12 < 1 - (real_t)CMP_EPSILON) {
			if (m12 > -(1 - (real_t)CMP_EPSILON)) {
				e.x = std::asin(-m12);
				e.y = std::atan2(rows[0].z, rows[2].z);
				e.z = std::atan2(rows[1].x, rows[1].y);
			} else { // m12 == -1
				e.x = (real_t)Math_PI * 0.5f;
				e.y = std::atan2(rows[0].y, rows[0].x);
				e.z = 0;
			}
		} else { // m12 == 1
			e.x = -(real_t)Math_PI * 0.5f;
			e.y = -std::atan2(rows[0].y, rows[0].x);
			e.z = 0;
		}
		return e;
	}
	// Base dont l'axe -Z pointe dans la direction donnée.
	static Basis looking_at(const Vector3 &p_direction, const Vector3 &p_up = Vector3(0, 1, 0)) {
		Vector3 z = (-p_direction).normalized();
		if (z.length_squared() < (real_t)CMP_EPSILON) {
			return Basis();
		}
		Vector3 x = p_up.cross(z);
		if (x.length_squared() < (real_t)CMP_EPSILON) {
			// Direction parallèle à « haut » : on choisit un autre axe de référence.
			x = Vector3(1, 0, 0).cross(z);
			if (x.length_squared() < (real_t)CMP_EPSILON) {
				x = Vector3(0, 0, 1).cross(z);
			}
		}
		x = x.normalized();
		const Vector3 y = z.cross(x);
		return Basis(x, y, z);
	}
};
