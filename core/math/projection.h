// Projection : matrice 4x4 (colonnes d'abord, comme OpenGL).
#pragma once

#include "core/math/transform_3d.h"

struct Projection {
	float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

	Projection() {}
	Projection(const Transform3D &t) {
		for (int c = 0; c < 3; c++) {
			const Vector3 col = t.basis.get_column(c);
			m[c * 4 + 0] = col.x;
			m[c * 4 + 1] = col.y;
			m[c * 4 + 2] = col.z;
			m[c * 4 + 3] = 0;
		}
		m[12] = t.origin.x;
		m[13] = t.origin.y;
		m[14] = t.origin.z;
		m[15] = 1;
	}

	static Projection perspective(float p_fovy_degrees, float p_aspect, float p_near, float p_far) {
		Projection p;
		const float f = 1.0f / std::tan(p_fovy_degrees * 0.5f * 3.14159265358979f / 180.0f);
		p.m[0] = f / p_aspect;
		p.m[5] = f;
		p.m[10] = (p_far + p_near) / (p_near - p_far);
		p.m[11] = -1;
		p.m[14] = 2 * p_far * p_near / (p_near - p_far);
		p.m[15] = 0;
		return p;
	}
	static Projection orthographic(float p_left, float p_right, float p_bottom, float p_top, float p_near, float p_far) {
		Projection p;
		p.m[0] = 2 / (p_right - p_left);
		p.m[5] = 2 / (p_top - p_bottom);
		p.m[10] = -2 / (p_far - p_near);
		p.m[12] = -(p_right + p_left) / (p_right - p_left);
		p.m[13] = -(p_top + p_bottom) / (p_top - p_bottom);
		p.m[14] = -(p_far + p_near) / (p_far - p_near);
		return p;
	}

	Projection operator*(const Projection &b) const {
		Projection r;
		for (int c = 0; c < 4; c++) {
			for (int row = 0; row < 4; row++) {
				float s = 0;
				for (int k = 0; k < 4; k++) {
					s += m[k * 4 + row] * b.m[c * 4 + k];
				}
				r.m[c * 4 + row] = s;
			}
		}
		return r;
	}
	// Transforme (x, y, z, 1) ; retourne le w dans r_w.
	Vector3 xform4(const Vector3 &v, float *r_w = nullptr) const {
		const float x = m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12];
		const float y = m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13];
		const float z = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14];
		const float w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15];
		if (r_w) {
			*r_w = w;
		}
		return Vector3(x, y, z);
	}
	// Transforme un point avec division perspective.
	Vector3 xform(const Vector3 &v) const {
		float w = 1;
		const Vector3 r = xform4(v, &w);
		return std::fabs(w) > 1e-12f ? r / w : r;
	}
	Projection inverse() const {
		Projection out;
		float inv[16];
		const float *a = m;
		inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
		inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
		inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
		inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
		inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
		inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
		inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
		inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
		inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
		inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
		inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
		inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
		inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
		inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
		inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
		inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
		const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
		if (std::fabs(det) < 1e-20f) {
			return out;
		}
		for (int i = 0; i < 16; i++) {
			out.m[i] = inv[i] / det;
		}
		return out;
	}
};
