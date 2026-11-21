// Constantes et fonctions mathématiques de base.
#pragma once

#include "core/typedefs.h"

#define Math_PI 3.14159265358979323846
#define Math_TAU 6.28318530717958647692
#define CMP_EPSILON 0.00001

namespace Math {
inline double deg_to_rad(double p_deg) { return p_deg * (Math_PI / 180.0); }
inline double rad_to_deg(double p_rad) { return p_rad * (180.0 / Math_PI); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
template <class T>
inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool is_equal_approx(double a, double b) {
	if (a == b) {
		return true;
	}
	double tolerance = CMP_EPSILON * std::fabs(a);
	if (tolerance < CMP_EPSILON) {
		tolerance = CMP_EPSILON;
	}
	return std::fabs(a - b) < tolerance;
}
inline bool is_zero_approx(double a) { return std::fabs(a) < CMP_EPSILON; }
inline double move_toward(double from, double to, double delta) {
	return std::fabs(to - from) <= delta ? to : from + (to > from ? delta : -delta);
}
inline double sign(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }
inline double fposmod(double x, double y) {
	double v = std::fmod(x, y);
	if ((v < 0 && y > 0) || (v > 0 && y < 0)) {
		v += y;
	}
	return v;
}
inline double wrapf(double v, double lo, double hi) {
	double range = hi - lo;
	return range == 0 ? lo : v - (range * std::floor((v - lo) / range));
}
inline double lerp_angle(double from, double to, double t) {
	double diff = std::fmod(to - from, Math_TAU);
	double dist = std::fmod(2.0 * diff, Math_TAU) - diff;
	return from + dist * t;
}
// Générateur pseudo-aléatoire (PCG simplifié).
uint32_t rand();
double randf(); // [0, 1]
double randf_range(double from, double to);
int64_t randi_range(int64_t from, int64_t to);
void seed(uint64_t p_seed);
void randomize();
} // namespace Math
