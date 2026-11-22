#pragma once

#include "core/math/math_defs.h"

struct Vector2 {
	real_t x = 0;
	real_t y = 0;

	Vector2() {}
	Vector2(real_t p_x, real_t p_y) :
			x(p_x), y(p_y) {}

	Vector2 operator+(const Vector2 &v) const { return Vector2(x + v.x, y + v.y); }
	Vector2 operator-(const Vector2 &v) const { return Vector2(x - v.x, y - v.y); }
	Vector2 operator*(const Vector2 &v) const { return Vector2(x * v.x, y * v.y); }
	Vector2 operator/(const Vector2 &v) const { return Vector2(x / v.x, y / v.y); }
	Vector2 operator*(real_t s) const { return Vector2(x * s, y * s); }
	Vector2 operator/(real_t s) const { return Vector2(x / s, y / s); }
	Vector2 operator-() const { return Vector2(-x, -y); }
	void operator+=(const Vector2 &v) { x += v.x; y += v.y; }
	void operator-=(const Vector2 &v) { x -= v.x; y -= v.y; }
	void operator*=(real_t s) { x *= s; y *= s; }
	void operator/=(real_t s) { x /= s; y /= s; }
	bool operator==(const Vector2 &v) const { return x == v.x && y == v.y; }
	bool operator!=(const Vector2 &v) const { return x != v.x || y != v.y; }
	bool operator<(const Vector2 &v) const { return x == v.x ? (y < v.y) : (x < v.x); }

	real_t length() const { return std::sqrt(x * x + y * y); }
	real_t length_squared() const { return x * x + y * y; }
	Vector2 normalized() const {
		real_t l = length();
		return l > 0 ? Vector2(x / l, y / l) : Vector2();
	}
	bool is_zero_approx() const { return Math::is_zero_approx(x) && Math::is_zero_approx(y); }
	bool is_equal_approx(const Vector2 &v) const { return Math::is_equal_approx(x, v.x) && Math::is_equal_approx(y, v.y); }
	real_t dot(const Vector2 &v) const { return x * v.x + y * v.y; }
	real_t cross(const Vector2 &v) const { return x * v.y - y * v.x; }
	real_t angle() const { return std::atan2(y, x); }
	real_t angle_to(const Vector2 &v) const { return std::atan2(cross(v), dot(v)); }
	real_t angle_to_point(const Vector2 &v) const { return (v - *this).angle(); }
	real_t distance_to(const Vector2 &v) const { return (v - *this).length(); }
	real_t distance_squared_to(const Vector2 &v) const { return (v - *this).length_squared(); }
	Vector2 direction_to(const Vector2 &v) const { return (v - *this).normalized(); }
	Vector2 rotated(real_t a) const {
		real_t s = std::sin(a), c = std::cos(a);
		return Vector2(x * c - y * s, x * s + y * c);
	}
	Vector2 orthogonal() const { return Vector2(y, -x); }
	Vector2 lerp(const Vector2 &to, real_t t) const { return Vector2(x + (to.x - x) * t, y + (to.y - y) * t); }
	Vector2 slide(const Vector2 &n) const { return *this - n * dot(n); }
	Vector2 reflect(const Vector2 &n) const { return n * (2.0f * dot(n)) - *this; }
	Vector2 bounce(const Vector2 &n) const { return -reflect(n); }
	Vector2 abs() const { return Vector2(std::fabs(x), std::fabs(y)); }
	Vector2 floor() const { return Vector2(std::floor(x), std::floor(y)); }
	Vector2 round() const { return Vector2(std::round(x), std::round(y)); }
	Vector2 sign() const { return Vector2((real_t)Math::sign(x), (real_t)Math::sign(y)); }
	Vector2 limit_length(real_t max_len = 1.0f) const {
		real_t l = length();
		return (l > 0 && l > max_len) ? *this * (max_len / l) : *this;
	}
	Vector2 move_toward(const Vector2 &to, real_t delta) const {
		Vector2 vd = to - *this;
		real_t len = vd.length();
		return (len <= delta || len < (real_t)CMP_EPSILON) ? to : *this + vd / len * delta;
	}
	Vector2 clamp(const Vector2 &lo, const Vector2 &hi) const {
		return Vector2(Math::clamp(x, lo.x, hi.x), Math::clamp(y, lo.y, hi.y));
	}
	static Vector2 from_angle(real_t a) { return Vector2(std::cos(a), std::sin(a)); }
};

inline Vector2 operator*(real_t s, const Vector2 &v) { return v * s; }

typedef Vector2 Size2;
typedef Vector2 Point2;
