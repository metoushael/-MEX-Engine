// Transform3D : base 3x3 + origine.
#pragma once

#include "core/math/basis.h"

struct Transform3D {
	Basis basis;
	Vector3 origin;

	Transform3D() {}
	Transform3D(const Basis &p_basis, const Vector3 &p_origin) :
			basis(p_basis), origin(p_origin) {}

	Vector3 xform(const Vector3 &v) const { return basis.xform(v) + origin; }
	Vector3 xform_inv(const Vector3 &v) const { return affine_inverse().xform(v); }
	Transform3D operator*(const Transform3D &t) const { return Transform3D(basis * t.basis, xform(t.origin)); }
	bool operator==(const Transform3D &t) const { return basis == t.basis && origin == t.origin; }
	bool operator!=(const Transform3D &t) const { return !(*this == t); }

	Transform3D affine_inverse() const {
		const Basis inv = basis.inverse();
		return Transform3D(inv, inv.xform(-origin));
	}
	Transform3D orthonormalized() const { return Transform3D(basis.orthonormalized(), origin); }
	Transform3D looking_at(const Vector3 &p_target, const Vector3 &p_up = Vector3(0, 1, 0)) const {
		return Transform3D(Basis::looking_at(p_target - origin, p_up), origin);
	}
};
