#pragma once

#include "core/math/math_defs.h"

struct Color {
	float r = 0, g = 0, b = 0, a = 1;

	Color() {}
	Color(float p_r, float p_g, float p_b, float p_a = 1.0f) :
			r(p_r), g(p_g), b(p_b), a(p_a) {}
	Color(const Color &c, float p_a) :
			r(c.r), g(c.g), b(c.b), a(p_a) {}

	Color operator*(const Color &c) const { return Color(r * c.r, g * c.g, b * c.b, a * c.a); }
	Color operator*(float s) const { return Color(r * s, g * s, b * s, a * s); }
	Color operator+(const Color &c) const { return Color(r + c.r, g + c.g, b + c.b, a + c.a); }
	Color operator-(const Color &c) const { return Color(r - c.r, g - c.g, b - c.b, a - c.a); }
	bool operator==(const Color &c) const { return r == c.r && g == c.g && b == c.b && a == c.a; }
	bool operator!=(const Color &c) const { return !(*this == c); }

	Color lerp(const Color &to, float t) const {
		return Color(r + (to.r - r) * t, g + (to.g - g) * t, b + (to.b - b) * t, a + (to.a - a) * t);
	}
	Color darkened(float amount) const { return Color(r * (1 - amount), g * (1 - amount), b * (1 - amount), a); }
	Color lightened(float amount) const {
		return Color(r + (1 - r) * amount, g + (1 - g) * amount, b + (1 - b) * amount, a);
	}

	uint32_t to_rgba32() const {
		auto c8 = [](float v) -> uint32_t { return (uint32_t)(Math::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
		return (c8(r) << 24) | (c8(g) << 16) | (c8(b) << 8) | c8(a);
	}
	String to_html(bool p_alpha = true) const;
	static Color html(const String &p_code); // "#rrggbb" ou "#rrggbbaa"
	static Color from_hsv(float h, float s, float v, float a = 1.0f);
	static bool find_named(const String &p_name, Color &r_color);
};
