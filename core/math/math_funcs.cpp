#include "core/math/color.h"
#include "core/math/math_defs.h"

#include <chrono>

namespace Math {
static uint64_t rng_state = 0x853c49e6748fea9bULL;
static uint64_t rng_inc = 0xda3e39cb94b95bdbULL;

uint32_t rand() {
	uint64_t old = rng_state;
	rng_state = old * 6364136223846793005ULL + (rng_inc | 1);
	uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
	uint32_t rot = (uint32_t)(old >> 59u);
	return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
}
double randf() { return (double)rand() / (double)0xFFFFFFFFu; }
double randf_range(double from, double to) { return from + (to - from) * randf(); }
int64_t randi_range(int64_t from, int64_t to) {
	if (to < from) {
		std::swap(from, to);
	}
	uint64_t range = (uint64_t)(to - from) + 1;
	return from + (int64_t)(rand() % range);
}
void seed(uint64_t p_seed) {
	rng_state = 0;
	rng_inc = (p_seed << 1) | 1;
	rand();
	rng_state += p_seed;
	rand();
}
void randomize() {
	seed((uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
}
} // namespace Math

String Color::to_html(bool p_alpha) const {
	uint32_t c = to_rgba32();
	char buf[16];
	if (p_alpha) {
		snprintf(buf, sizeof(buf), "#%08x", c);
	} else {
		snprintf(buf, sizeof(buf), "#%06x", c >> 8);
	}
	return buf;
}

Color Color::html(const String &p_code) {
	String s = p_code;
	if (!s.empty() && s[0] == '#') {
		s = s.substr(1);
	}
	auto hex = [&](size_t i) -> float {
		if (i + 1 >= s.size() + 0 && i + 2 > s.size()) {
			return 1.0f;
		}
		return (float)strtol(s.substr(i, 2).c_str(), nullptr, 16) / 255.0f;
	};
	if (s.size() == 6) {
		return Color(hex(0), hex(2), hex(4), 1.0f);
	}
	if (s.size() == 8) {
		return Color(hex(0), hex(2), hex(4), hex(6));
	}
	return Color(0, 0, 0, 1);
}

Color Color::from_hsv(float h, float s, float v, float a) {
	h = (float)Math::fposmod(h, 1.0) * 6.0f;
	int i = (int)std::floor(h);
	float f = h - i;
	float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
	switch (i % 6) {
		case 0: return Color(v, t, p, a);
		case 1: return Color(q, v, p, a);
		case 2: return Color(p, v, t, a);
		case 3: return Color(p, q, v, a);
		case 4: return Color(t, p, v, a);
		default: return Color(v, p, q, a);
	}
}

bool Color::find_named(const String &p_name, Color &r_color) {
	static const std::map<String, Color> named = {
		{ "WHITE", Color(1, 1, 1) }, { "BLACK", Color(0, 0, 0) }, { "RED", Color(1, 0, 0) },
		{ "GREEN", Color(0, 1, 0) }, { "BLUE", Color(0, 0, 1) }, { "YELLOW", Color(1, 1, 0) },
		{ "CYAN", Color(0, 1, 1) }, { "MAGENTA", Color(1, 0, 1) }, { "ORANGE", Color(1, 0.65f, 0) },
		{ "PURPLE", Color(0.63f, 0.13f, 0.94f) }, { "PINK", Color(1, 0.75f, 0.8f) }, { "GRAY", Color(0.75f, 0.75f, 0.75f) },
		{ "DARK_GRAY", Color(0.66f, 0.66f, 0.66f) }, { "DIM_GRAY", Color(0.41f, 0.41f, 0.41f) },
		{ "BROWN", Color(0.65f, 0.16f, 0.16f) }, { "GOLD", Color(1, 0.84f, 0) }, { "SKY_BLUE", Color(0.53f, 0.81f, 0.92f) },
		{ "LIME", Color(0, 1, 0) }, { "DARK_GREEN", Color(0, 0.39f, 0) }, { "DARK_BLUE", Color(0, 0, 0.55f) },
		{ "DARK_RED", Color(0.55f, 0, 0) }, { "TRANSPARENT", Color(1, 1, 1, 0) },
	};
	auto it = named.find(p_name);
	if (it == named.end()) {
		return false;
	}
	r_color = it->second;
	return true;
}
