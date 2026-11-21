#pragma once

#include "core/math/math_defs.h"

struct Vector3 {
	real_t x = 0;
	real_t y = 0;
	real_t z = 0;

	Vector3() {}
	Vector3(real_t p_x, real_t p_y, real_t p_z) :
			x(p_x), y(p_y), z(p_z) {}

	real_t &operator[](int p_axis) { return p_axis == 0 ? x : (p_axis == 1 ? y : z); }
	real_t operator[](int p_axis) const { return p_axis == 0 ? x : (p_axis == 1 ? y : z); }

	Vector3 operator+(const Vector3 &v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
	Vector3 operator-(const Vector3 &v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
	Vector3 operator*(const Vector3 &v) const { return Vector3(x * v.x, y * v.y, z * v.z); }
	Vector3 operator/(const Vector3 &v) const { return Vector3(x / v.x, y / v.y, z / v.z); }
	Vector3 operator*(real_t s) const { return Vector3(x * s, y * s, z * s); }
	Vector3 operator/(real_t s) const { return Vector3(x / s, y / s, z / s); }
	Vector3 operator-() const { return Vector3(-x, -y, -z); }
	void operator+=(const Vector3 &v) { x += v.x; y += v.y; z += v.z; }
	void operator-=(const Vector3 &v) { x -= v.x; y -= v.y; z -= v.z; }
	void operator*=(real_t s) { x *= s; y *= s; z *= s; }
	void operator/=(real_t s) { x /= s; y /= s; z /= s; }
	bool operator==(const Vector3 &v) const { return x == v.x && y == v.y && z == v.z; }
	bool operator!=(const Vector3 &v) const { return !(*this == v); }
	bool operator<(const Vector3 &v) const { return x != v.x ? x < v.x : (y != v.y ? y < v.y : z < v.z); }

	real_t length() const { return std::sqrt(x * x + y * y + z * z); }
	real_t length_squared() const { return x * x + y * y + z * z; }
	Vector3 normalized() const {
		const real_t l = length();
		return l > 0 ? Vector3(x / l, y / l, z / l) : Vector3();
	}
	bool is_zero_approx() const { return Math::is_zero_approx(x) && Math::is_zero_approx(y) && Math::is_zero_approx(z); }
	bool is_equal_approx(const Vector3 &v) const { return Math::is_equal_approx(x, v.x) && Math::is_equal_approx(y, v.y) && Math::is_equal_approx(z, v.z); }
	real_t dot(const Vector3 &v) const { return x * v.x + y * v.y + z * v.z; }
	Vector3 cross(const Vector3 &v) const { return Vector3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x); }
	real_t distance_to(const Vector3 &v) const { return (v - *this).length(); }
	real_t distance_squared_to(const Vector3 &v) const { return (v - *this).length_squared(); }
	Vector3 direction_to(const Vector3 &v) const { return (v - *this).normalized(); }
	real_t angle_to(const Vector3 &v) const { return std::atan2(cross(v).length(), dot(v)); }
	Vector3 lerp(const Vector3 &to, real_t t) const { return Vector3(x + (to.x - x) * t, y + (to.y - y) * t, z + (to.z - z) * t); }
	Vector3 slide(const Vector3 &n) const { return *this - n * dot(n); }
	Vector3 reflect(const Vector3 &n) const { return n * (2.0f * dot(n)) - *this; }
	Vector3 bounce(const Vector3 &n) const { return -reflect(n); }
	Vector3 project(const Vector3 &v) const {
		const real_t d = v.length_squared();
		return d > 0 ? v * (dot(v) / d) : Vector3();
	}
	Vector3 abs() const { return Vector3(std::fabs(x), std::fabs(y), std::fabs(z)); }
	Vector3 floor() const { return Vector3(std::floor(x), std::floor(y), std::floor(z)); }
	Vector3 round() const { return Vector3(std::round(x), std::round(y), std::round(z)); }
	Vector3 sign() const { return Vector3((real_t)Math::sign(x), (real_t)Math::sign(y), (real_t)Math::sign(z)); }
	Vector3 limit_length(real_t max_len = 1.0f) const {
		const real_t l = length();
		return (l > 0 && l > max_len) ? *this * (max_len / l) : *this;
	}
	Vector3 move_toward(const Vector3 &to, real_t delta) const {
		const Vector3 vd = to - *this;
		const real_t len = vd.length();
		return (len <= delta || len < (real_t)CMP_EPSILON) ? to : *this + vd / len * delta;
	}
	Vector3 clamp(const Vector3 &lo, const Vector3 &hi) const {
		return Vector3(Math::clamp(x, lo.x, hi.x), Math::clamp(y, lo.y, hi.y), Math::clamp(z, lo.z, hi.z));
	}
	// Rotation autour d'un axe unitaire (formule de Rodrigues).
	Vector3 rotated(const Vector3 &axis, real_t angle) const {
		const real_t s = std::sin(angle), c = std::cos(angle);
		return *this * c + axis.cross(*this) * s + axis * (axis.dot(*this) * (1 - c));
	}
	int min_axis_index() const { return x < y ? (x < z ? 0 : 2) : (y < z ? 1 : 2); }
	int max_axis_index() const { return x < y ? (y < z ? 2 : 1) : (x < z ? 2 : 0); }
};

inline Vector3 operator*(real_t s, const Vector3 &v) { return v * s; }
