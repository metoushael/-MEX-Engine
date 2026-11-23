// Fonctions et constantes globales de MexScript.
#include "mexscript.h"

#include "core/debug_monitor.h"
#include "core/input/keyboard.h"
#include "core/string/translation.h"
#include "core/string/ustring.h"

namespace {

#define ARG(m_i) (*p_args[m_i])
#define CHECK_ARGS(m_min, m_max)                                                                         \
	if (p_argc < m_min || p_argc > m_max) {                                                              \
		r_ctx.error = m_min == m_max                                                                     \
				? vformat("%d argument(s) attendu(s), %d fourni(s).", m_min, p_argc)                     \
				: vformat("entre %d et %d arguments attendus, %d fourni(s).", m_min, m_max, p_argc);     \
		return Variant();                                                                                \
	}
#define BUILTIN(m_name) Variant fn_##m_name(const Variant **p_args, int p_argc, MXBuiltinContext &r_ctx)

String _join_args(const Variant **p_args, int p_argc, const char *p_sep) {
	String out;
	for (int i = 0; i < p_argc; i++) {
		if (i > 0) {
			out += p_sep;
		}
		out += p_args[i]->stringify();
	}
	return out;
}

BUILTIN(print) {
	print_line(_join_args(p_args, p_argc, ""));
	return Variant();
}
BUILTIN(prints) {
	print_line(_join_args(p_args, p_argc, " "));
	return Variant();
}
BUILTIN(printerr) {
	print_error(_join_args(p_args, p_argc, ""));
	return Variant();
}
BUILTIN(push_warning) {
	print_warning(_join_args(p_args, p_argc, ""));
	return Variant();
}
BUILTIN(str) {
	return Variant(_join_args(p_args, p_argc, ""));
}
BUILTIN(to_int) {
	CHECK_ARGS(1, 1);
	if (ARG(0).get_type() == Variant::STRING) {
		return Variant((int64_t)strtoll(ARG(0).as_string().c_str(), nullptr, 10));
	}
	return Variant(ARG(0).as_int());
}
BUILTIN(to_float) {
	CHECK_ARGS(1, 1);
	if (ARG(0).get_type() == Variant::STRING) {
		return Variant(strtod(ARG(0).as_string().c_str(), nullptr));
	}
	return Variant(ARG(0).as_float());
}
BUILTIN(to_bool) {
	CHECK_ARGS(1, 1);
	return Variant(ARG(0).booleanize());
}
BUILTIN(tr) {
	CHECK_ARGS(1, 1);
	return Variant(TTR_GAME(ARG(0).as_string()));
}
BUILTIN(get_stack) {
	CHECK_ARGS(0, 0);
	return Variant(Performance::get_singleton() ? Performance::get_singleton()->get_script_stack() : Array());
}
BUILTIN(print_stack) {
	CHECK_ARGS(0, 0);
	std::vector<Performance::ScriptStackEntry> stack;
	if (Performance::script_get_stack) {
		Performance::script_get_stack(&stack);
	}
	for (size_t i = 0; i < stack.size(); i++) {
		print_line(vformat("  [%d] %s (%s:%d)", (int)i, stack[i].function.c_str(), stack[i].source.c_str(), stack[i].line));
	}
	return Variant();
}
BUILTIN(len) {
	CHECK_ARGS(1, 1);
	switch (ARG(0).get_type()) {
		case Variant::STRING: return Variant((int64_t)utf8_decode(ARG(0).as_string()).size());
		case Variant::ARRAY: return Variant(ARG(0).as_array().size());
		case Variant::DICTIONARY: return Variant(ARG(0).as_dictionary().size());
		default:
			r_ctx.error = "len() attend une chaîne, un tableau ou un dictionnaire.";
			return Variant();
	}
}
BUILTIN(range) {
	CHECK_ARGS(1, 3);
	int64_t from = 0, to = 0, step = 1;
	if (p_argc == 1) {
		to = ARG(0).as_int();
	} else {
		from = ARG(0).as_int();
		to = ARG(1).as_int();
		if (p_argc == 3) {
			step = ARG(2).as_int();
		}
	}
	if (step == 0) {
		r_ctx.error = "range() : le pas ne peut pas être nul.";
		return Variant();
	}
	Array out;
	if (step > 0) {
		for (int64_t i = from; i < to; i += step) {
			out.push_back(Variant(i));
		}
	} else {
		for (int64_t i = from; i > to; i += step) {
			out.push_back(Variant(i));
		}
	}
	return Variant(out);
}
BUILTIN(typeof_) {
	CHECK_ARGS(1, 1);
	return Variant((int64_t)ARG(0).get_type());
}
BUILTIN(type_string) {
	CHECK_ARGS(1, 1);
	return Variant(Variant::get_type_name((Variant::Type)ARG(0).as_int()));
}
BUILTIN(is_instance_valid) {
	CHECK_ARGS(1, 1);
	return Variant(ARG(0).get_type() == Variant::OBJECT && !ARG(0).is_null());
}
BUILTIN(load) {
	CHECK_ARGS(1, 1);
	Ref<Resource> res = ResourceLoader::load(ARG(0).as_string());
	if (res.is_null()) {
		r_ctx.error = "Impossible de charger la ressource : " + ARG(0).as_string();
		return Variant();
	}
	return Variant(res);
}

// --- Constructeurs ---
BUILTIN(Vector2) {
	if (p_argc == 0) {
		return Variant(::Vector2());
	}
	if (p_argc == 1 && ARG(0).get_type() == Variant::VECTOR2) {
		return ARG(0);
	}
	CHECK_ARGS(2, 2);
	return Variant(::Vector2((real_t)ARG(0).as_float(), (real_t)ARG(1).as_float()));
}
BUILTIN(Vector3) {
	if (p_argc == 0) {
		return Variant(::Vector3());
	}
	if (p_argc == 1 && ARG(0).get_type() == Variant::VECTOR3) {
		return ARG(0);
	}
	CHECK_ARGS(3, 3);
	return Variant(::Vector3((real_t)ARG(0).as_float(), (real_t)ARG(1).as_float(), (real_t)ARG(2).as_float()));
}
BUILTIN(Rect2) {
	if (p_argc == 0) {
		return Variant(::Rect2());
	}
	if (p_argc == 2) {
		return Variant(::Rect2(ARG(0).as_vector2(), ARG(1).as_vector2()));
	}
	CHECK_ARGS(4, 4);
	return Variant(::Rect2((real_t)ARG(0).as_float(), (real_t)ARG(1).as_float(), (real_t)ARG(2).as_float(), (real_t)ARG(3).as_float()));
}
BUILTIN(Color) {
	if (p_argc == 0) {
		return Variant(::Color(0, 0, 0, 1));
	}
	if (ARG(0).get_type() == Variant::STRING) {
		CHECK_ARGS(1, 2);
		String s = ARG(0).as_string();
		::Color c;
		if (!::Color::find_named(str_to_upper(s), c)) {
			c = ::Color::html(s);
		}
		if (p_argc == 2) {
			c.a = (float)ARG(1).as_float();
		}
		return Variant(c);
	}
	if (ARG(0).get_type() == Variant::COLOR) {
		CHECK_ARGS(1, 2);
		::Color c = ARG(0).as_color();
		if (p_argc == 2) {
			c.a = (float)ARG(1).as_float();
		}
		return Variant(c);
	}
	CHECK_ARGS(3, 4);
	return Variant(::Color((float)ARG(0).as_float(), (float)ARG(1).as_float(), (float)ARG(2).as_float(), p_argc == 4 ? (float)ARG(3).as_float() : 1.0f));
}
BUILTIN(Array) {
	if (p_argc == 1 && ARG(0).get_type() == Variant::ARRAY) {
		return Variant(ARG(0).as_array().duplicate());
	}
	CHECK_ARGS(0, 0);
	return Variant(::Array());
}
BUILTIN(Dictionary) {
	if (p_argc == 1 && ARG(0).get_type() == Variant::DICTIONARY) {
		return Variant(ARG(0).as_dictionary().duplicate());
	}
	CHECK_ARGS(0, 0);
	return Variant(::Dictionary());
}

// --- Mathématiques ---
#define MATH1(m_name, m_expr)        \
	BUILTIN(m_name) {                \
		CHECK_ARGS(1, 1);            \
		double x = ARG(0).as_float(); \
		return Variant((double)(m_expr)); \
	}
#define MATH2(m_name, m_expr)        \
	BUILTIN(m_name) {                \
		CHECK_ARGS(2, 2);            \
		double x = ARG(0).as_float(); \
		double y = ARG(1).as_float(); \
		return Variant((double)(m_expr)); \
	}

MATH1(sqrt, std::sqrt(x))
MATH1(sin, std::sin(x))
MATH1(cos, std::cos(x))
MATH1(tan, std::tan(x))
MATH1(asin, std::asin(x))
MATH1(acos, std::acos(x))
MATH1(atan, std::atan(x))
MATH1(exp, std::exp(x))
MATH1(log, std::log(x))
MATH1(deg_to_rad, Math::deg_to_rad(x))
MATH1(rad_to_deg, Math::rad_to_deg(x))
MATH1(absf, std::fabs(x))
MATH1(signf, Math::sign(x))
MATH2(atan2, std::atan2(x, y))
MATH2(pow, std::pow(x, y))
MATH2(fmod, std::fmod(x, y))
MATH2(fposmod, Math::fposmod(x, y))

BUILTIN(floor) {
	CHECK_ARGS(1, 1);
	if (ARG(0).get_type() == Variant::VECTOR2) {
		return Variant(ARG(0).as_vector2().floor());
	}
	if (ARG(0).get_type() == Variant::VECTOR3) {
		return Variant(ARG(0).as_vector3().floor());
	}
	return Variant(std::floor(ARG(0).as_float()));
}
BUILTIN(ceil) {
	CHECK_ARGS(1, 1);
	return Variant(std::ceil(ARG(0).as_float()));
}
BUILTIN(round) {
	CHECK_ARGS(1, 1);
	if (ARG(0).get_type() == Variant::VECTOR2) {
		return Variant(ARG(0).as_vector2().round());
	}
	if (ARG(0).get_type() == Variant::VECTOR3) {
		return Variant(ARG(0).as_vector3().round());
	}
	return Variant(std::round(ARG(0).as_float()));
}
BUILTIN(floori) {
	CHECK_ARGS(1, 1);
	return Variant((int64_t)std::floor(ARG(0).as_float()));
}
BUILTIN(ceili) {
	CHECK_ARGS(1, 1);
	return Variant((int64_t)std::ceil(ARG(0).as_float()));
}
BUILTIN(roundi) {
	CHECK_ARGS(1, 1);
	return Variant((int64_t)std::llround(ARG(0).as_float()));
}
BUILTIN(abs) {
	CHECK_ARGS(1, 1);
	switch (ARG(0).get_type()) {
		case Variant::INT: return Variant((int64_t)std::llabs(ARG(0).as_int()));
		case Variant::VECTOR2: return Variant(ARG(0).as_vector2().abs());
		case Variant::VECTOR3: return Variant(ARG(0).as_vector3().abs());
		default: return Variant(std::fabs(ARG(0).as_float()));
	}
}
BUILTIN(absi) {
	CHECK_ARGS(1, 1);
	return Variant((int64_t)std::llabs(ARG(0).as_int()));
}
BUILTIN(sign) {
	CHECK_ARGS(1, 1);
	if (ARG(0).get_type() == Variant::INT) {
		int64_t v = ARG(0).as_int();
		return Variant((int64_t)(v > 0 ? 1 : (v < 0 ? -1 : 0)));
	}
	if (ARG(0).get_type() == Variant::VECTOR2) {
		return Variant(ARG(0).as_vector2().sign());
	}
	if (ARG(0).get_type() == Variant::VECTOR3) {
		return Variant(ARG(0).as_vector3().sign());
	}
	return Variant(Math::sign(ARG(0).as_float()));
}
BUILTIN(signi) {
	CHECK_ARGS(1, 1);
	int64_t v = ARG(0).as_int();
	return Variant((int64_t)(v > 0 ? 1 : (v < 0 ? -1 : 0)));
}

bool _all_ints(const Variant **p_args, int p_argc) {
	for (int i = 0; i < p_argc; i++) {
		if (p_args[i]->get_type() != Variant::INT) {
			return false;
		}
	}
	return true;
}

BUILTIN(min) {
	if (p_argc < 2) {
		r_ctx.error = "min() attend au moins 2 arguments.";
		return Variant();
	}
	if (_all_ints(p_args, p_argc)) {
		int64_t m = ARG(0).as_int();
		for (int i = 1; i < p_argc; i++) {
			m = std::min(m, ARG(i).as_int());
		}
		return Variant(m);
	}
	double m = ARG(0).as_float();
	for (int i = 1; i < p_argc; i++) {
		m = std::min(m, ARG(i).as_float());
	}
	return Variant(m);
}
BUILTIN(max) {
	if (p_argc < 2) {
		r_ctx.error = "max() attend au moins 2 arguments.";
		return Variant();
	}
	if (_all_ints(p_args, p_argc)) {
		int64_t m = ARG(0).as_int();
		for (int i = 1; i < p_argc; i++) {
			m = std::max(m, ARG(i).as_int());
		}
		return Variant(m);
	}
	double m = ARG(0).as_float();
	for (int i = 1; i < p_argc; i++) {
		m = std::max(m, ARG(i).as_float());
	}
	return Variant(m);
}
BUILTIN(clamp) {
	CHECK_ARGS(3, 3);
	if (ARG(0).get_type() == Variant::VECTOR2) {
		return Variant(ARG(0).as_vector2().clamp(ARG(1).as_vector2(), ARG(2).as_vector2()));
	}
	if (ARG(0).get_type() == Variant::VECTOR3) {
		return Variant(ARG(0).as_vector3().clamp(ARG(1).as_vector3(), ARG(2).as_vector3()));
	}
	if (_all_ints(p_args, p_argc)) {
		return Variant(Math::clamp(ARG(0).as_int(), ARG(1).as_int(), ARG(2).as_int()));
	}
	return Variant(Math::clamp(ARG(0).as_float(), ARG(1).as_float(), ARG(2).as_float()));
}
BUILTIN(clampf) {
	CHECK_ARGS(3, 3);
	return Variant(Math::clamp(ARG(0).as_float(), ARG(1).as_float(), ARG(2).as_float()));
}
BUILTIN(clampi) {
	CHECK_ARGS(3, 3);
	return Variant(Math::clamp(ARG(0).as_int(), ARG(1).as_int(), ARG(2).as_int()));
}
BUILTIN(lerp) {
	CHECK_ARGS(3, 3);
	const double t = ARG(2).as_float();
	switch (ARG(0).get_type()) {
		case Variant::VECTOR2: return Variant(ARG(0).as_vector2().lerp(ARG(1).as_vector2(), (real_t)t));
		case Variant::VECTOR3: return Variant(ARG(0).as_vector3().lerp(ARG(1).as_vector3(), (real_t)t));
		case Variant::COLOR: {
			::Color a = ARG(0).as_color(), b = ARG(1).as_color();
			float f = (float)t;
			return Variant(::Color(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f));
		}
		default: return Variant(Math::lerp(ARG(0).as_float(), ARG(1).as_float(), t));
	}
}
BUILTIN(lerp_angle) {
	CHECK_ARGS(3, 3);
	return Variant(Math::lerp_angle(ARG(0).as_float(), ARG(1).as_float(), ARG(2).as_float()));
}
BUILTIN(inverse_lerp) {
	CHECK_ARGS(3, 3);
	double a = ARG(0).as_float(), b = ARG(1).as_float();
	return Variant(a == b ? 0.0 : (ARG(2).as_float() - a) / (b - a));
}
BUILTIN(remap) {
	CHECK_ARGS(5, 5);
	double v = ARG(0).as_float(), a = ARG(1).as_float(), b = ARG(2).as_float(), c = ARG(3).as_float(), d = ARG(4).as_float();
	return Variant(a == b ? c : c + (d - c) * ((v - a) / (b - a)));
}
// Fonctions mathématiques portées de core/math/math_funcs.{h,cpp} du moteur Godot 4.3
// (licence MIT, voir thirdparty/godot/LICENSE.txt).
BUILTIN(ease) {
	CHECK_ARGS(2, 2);
	double x = Math::clamp(ARG(0).as_float(), 0.0, 1.0);
	const double c = ARG(1).as_float();
	if (c > 0) {
		return Variant(c < 1.0 ? 1.0 - std::pow(1.0 - x, 1.0 / c) : std::pow(x, c));
	} else if (c < 0) {
		// inout ease
		if (x < 0.5) {
			return Variant(std::pow(x * 2.0, -c) * 0.5);
		}
		return Variant((1.0 - std::pow(1.0 - (x - 0.5) * 2.0, -c)) * 0.5 + 0.5);
	}
	return Variant(0.0); // no ease (raw)
}
static double _angle_difference(double p_from, double p_to) {
	const double tau = 6.283185307179586476925286766559;
	const double difference = std::fmod(p_to - p_from, tau);
	return std::fmod(2.0 * difference, tau) - difference;
}
BUILTIN(angle_difference) {
	CHECK_ARGS(2, 2);
	return Variant(_angle_difference(ARG(0).as_float(), ARG(1).as_float()));
}
BUILTIN(rotate_toward) {
	CHECK_ARGS(3, 3);
	const double from = ARG(0).as_float(), to = ARG(1).as_float(), delta = ARG(2).as_float();
	const double difference = _angle_difference(from, to);
	const double abs_difference = std::abs(difference);
	// When `delta < 0` move no further than to PI radians away from `to` (as PI is the max possible angle distance).
	return Variant(from + Math::clamp(delta, abs_difference - 3.14159265358979323846, abs_difference) * (difference >= 0.0 ? 1.0 : -1.0));
}
BUILTIN(cubic_interpolate) {
	CHECK_ARGS(5, 5);
	const double from = ARG(0).as_float(), to = ARG(1).as_float(), pre = ARG(2).as_float(), post = ARG(3).as_float(), weight = ARG(4).as_float();
	return Variant(0.5 *
			((from * 2.0) +
					(-pre + to) * weight +
					(2.0 * pre - 5.0 * from + 4.0 * to - post) * (weight * weight) +
					(-pre + 3.0 * from - 3.0 * to + post) * (weight * weight * weight)));
}
BUILTIN(bezier_interpolate) {
	CHECK_ARGS(5, 5);
	const double start = ARG(0).as_float(), control_1 = ARG(1).as_float(), control_2 = ARG(2).as_float(), end = ARG(3).as_float(), t = ARG(4).as_float();
	/* Formula from Wikipedia article on Bezier curves. */
	const double omt = (1.0 - t);
	const double omt2 = omt * omt;
	const double omt3 = omt2 * omt;
	const double t2 = t * t;
	const double t3 = t2 * t;
	return Variant(start * omt3 + control_1 * omt2 * t * 3.0 + control_2 * omt * t2 * 3.0 + end * t3);
}
BUILTIN(db_to_linear) {
	CHECK_ARGS(1, 1);
	return Variant(std::exp(ARG(0).as_float() * 0.11512925464970228420089957273422));
}
BUILTIN(linear_to_db) {
	CHECK_ARGS(1, 1);
	return Variant(std::log(ARG(0).as_float()) * 8.6858896380650365530225783783321);
}
BUILTIN(posmod) {
	CHECK_ARGS(2, 2);
	const int64_t x = ARG(0).as_int(), y = ARG(1).as_int();
	if (y == 0) {
		return Variant((int64_t)0);
	}
	int64_t value = x % y;
	if (((value < 0) && (y > 0)) || ((value > 0) && (y < 0))) {
		value += y;
	}
	return Variant(value);
}
BUILTIN(nearest_po2) {
	CHECK_ARGS(1, 1);
	int64_t x = ARG(0).as_int();
	if (x <= 0) {
		return Variant((int64_t)0);
	}
	int64_t power = 1;
	while (power < x && power < ((int64_t)1 << 62)) {
		power <<= 1;
	}
	return Variant(power);
}
BUILTIN(is_inf) {
	CHECK_ARGS(1, 1);
	return Variant(std::isinf(ARG(0).as_float()));
}
BUILTIN(is_finite) {
	CHECK_ARGS(1, 1);
	return Variant(std::isfinite(ARG(0).as_float()));
}
MATH1(sinh, std::sinh(x))
MATH1(cosh, std::cosh(x))
MATH1(tanh, std::tanh(x))

BUILTIN(smoothstep) {
	CHECK_ARGS(3, 3);
	double from = ARG(0).as_float(), to = ARG(1).as_float(), x = ARG(2).as_float();
	if (from == to) {
		return Variant(x >= to ? 1.0 : 0.0);
	}
	double t = Math::clamp((x - from) / (to - from), 0.0, 1.0);
	return Variant(t * t * (3.0 - 2.0 * t));
}
BUILTIN(move_toward) {
	CHECK_ARGS(3, 3);
	return Variant(Math::move_toward(ARG(0).as_float(), ARG(1).as_float(), ARG(2).as_float()));
}
BUILTIN(wrapf) {
	CHECK_ARGS(3, 3);
	return Variant(Math::wrapf(ARG(0).as_float(), ARG(1).as_float(), ARG(2).as_float()));
}
BUILTIN(wrapi) {
	CHECK_ARGS(3, 3);
	int64_t v = ARG(0).as_int(), lo = ARG(1).as_int(), hi = ARG(2).as_int();
	int64_t range = hi - lo;
	if (range == 0) {
		return Variant(lo);
	}
	return Variant(lo + ((((v - lo) % range) + range) % range));
}
BUILTIN(snapped) {
	CHECK_ARGS(2, 2);
	double step = ARG(1).as_float();
	double v = ARG(0).as_float();
	double out = step != 0 ? std::floor(v / step + 0.5) * step : v;
	if (_all_ints(p_args, p_argc)) {
		return Variant((int64_t)out);
	}
	return Variant(out);
}
BUILTIN(pingpong) {
	CHECK_ARGS(2, 2);
	double length = ARG(1).as_float();
	if (length == 0) {
		return Variant(0.0);
	}
	return Variant(std::fabs(Math::fposmod(ARG(0).as_float() - length, length * 2.0) - length));
}
BUILTIN(is_equal_approx) {
	CHECK_ARGS(2, 2);
	return Variant(Math::is_equal_approx(ARG(0).as_float(), ARG(1).as_float()));
}
BUILTIN(is_zero_approx) {
	CHECK_ARGS(1, 1);
	return Variant(Math::is_zero_approx(ARG(0).as_float()));
}
BUILTIN(is_nan) {
	CHECK_ARGS(1, 1);
	return Variant(std::isnan(ARG(0).as_float()));
}

// --- Aléatoire ---
BUILTIN(randf) {
	CHECK_ARGS(0, 0);
	return Variant(Math::randf());
}
BUILTIN(randi) {
	CHECK_ARGS(0, 0);
	return Variant((int64_t)Math::rand());
}
BUILTIN(randf_range) {
	CHECK_ARGS(2, 2);
	return Variant(Math::randf_range(ARG(0).as_float(), ARG(1).as_float()));
}
BUILTIN(randi_range) {
	CHECK_ARGS(2, 2);
	return Variant(Math::randi_range(ARG(0).as_int(), ARG(1).as_int()));
}
BUILTIN(randomize) {
	CHECK_ARGS(0, 0);
	Math::randomize();
	return Variant();
}
BUILTIN(seed) {
	CHECK_ARGS(1, 1);
	Math::seed((uint64_t)ARG(0).as_int());
	return Variant();
}

const std::unordered_map<String, MXBuiltinFunction> &_builtins() {
#define REG(m_name) { #m_name, &fn_##m_name }
	static const std::unordered_map<String, MXBuiltinFunction> map = {
		REG(print), REG(prints), REG(printerr), { "push_error", &fn_printerr }, REG(push_warning), REG(str),
		{ "int", &fn_to_int }, { "float", &fn_to_float }, { "bool", &fn_to_bool }, { "String", &fn_str },
		REG(len), REG(range), { "typeof", &fn_typeof_ }, REG(type_string), REG(is_instance_valid),
		REG(load), { "preload", &fn_load }, REG(tr), REG(get_stack), REG(print_stack),
		REG(Vector2), REG(Vector3), REG(Rect2), REG(Color), REG(Array), REG(Dictionary),
		REG(sqrt), REG(sin), REG(cos), REG(tan), REG(asin), REG(acos), REG(atan), REG(atan2), REG(exp), REG(log), REG(pow),
		REG(deg_to_rad), REG(rad_to_deg), REG(fmod), REG(fposmod),
		REG(floor), REG(ceil), REG(round), REG(floori), REG(ceili), REG(roundi),
		REG(abs), REG(absf), REG(absi), REG(sign), REG(signf), REG(signi),
		REG(min), REG(max), { "minf", &fn_min }, { "maxf", &fn_max }, { "mini", &fn_min }, { "maxi", &fn_max },
		REG(clamp), REG(clampf), REG(clampi),
		REG(lerp), { "lerpf", &fn_lerp }, REG(lerp_angle), REG(inverse_lerp), REG(remap), REG(smoothstep), REG(move_toward),
		REG(wrapf), REG(wrapi), REG(snapped), { "snappedf", &fn_snapped }, REG(pingpong),
		REG(is_equal_approx), REG(is_zero_approx), REG(is_nan), REG(is_inf), REG(is_finite),
		REG(ease), REG(angle_difference), REG(rotate_toward), REG(cubic_interpolate), REG(bezier_interpolate),
		REG(db_to_linear), REG(linear_to_db), REG(posmod), REG(nearest_po2), REG(sinh), REG(cosh), REG(tanh),
		REG(randf), REG(randi), REG(randf_range), REG(randi_range), REG(randomize), REG(seed),
	};
#undef REG
	return map;
}

} // namespace

MXBuiltinFunction mexscript_find_builtin(const String &p_name) {
	const auto &map = _builtins();
	auto it = map.find(p_name);
	return it == map.end() ? nullptr : it->second;
}

void mexscript_get_builtin_names(std::vector<String> *r_names) {
	for (auto &kv : _builtins()) {
		r_names->push_back(kv.first);
	}
	std::sort(r_names->begin(), r_names->end());
}

bool mexscript_find_global_constant(const String &p_name, Variant &r_value) {
	static std::unordered_map<String, Variant> constants;
	if (constants.empty()) {
		constants["PI"] = Variant(Math_PI);
		constants["TAU"] = Variant(Math_TAU);
		constants["INF"] = Variant((double)INFINITY);
		constants["NAN"] = Variant((double)NAN);
		constants["OK"] = Variant((int64_t)OK);
		constants["FAILED"] = Variant((int64_t)FAILED);
		constants["MOUSE_BUTTON_LEFT"] = Variant((int64_t)MOUSE_BUTTON_LEFT);
		constants["MOUSE_BUTTON_RIGHT"] = Variant((int64_t)MOUSE_BUTTON_RIGHT);
		constants["MOUSE_BUTTON_MIDDLE"] = Variant((int64_t)MOUSE_BUTTON_MIDDLE);
		constants["MOUSE_BUTTON_WHEEL_UP"] = Variant((int64_t)MOUSE_BUTTON_WHEEL_UP);
		constants["MOUSE_BUTTON_WHEEL_DOWN"] = Variant((int64_t)MOUSE_BUTTON_WHEEL_DOWN);
		// KEY_SPACE, KEY_A, KEY_LEFT... à partir des noms connus de l'InputMap.
		std::vector<std::pair<String, int>> codes;
		input_code_get_all_names(&codes);
		for (const auto &kv : codes) {
			if (kv.second == 0 || (kv.second & INPUT_CODE_MOUSE_FLAG)) {
				continue;
			}
			constants["KEY_" + str_to_upper(str_replace(kv.first, " ", "_"))] = Variant((int64_t)kv.second);
		}
		// Manette : mêmes noms que dans Godot.
		const std::pair<const char *, int> joy_constants[] = {
			{ "JOY_BUTTON_A", JOY_BUTTON_A }, { "JOY_BUTTON_B", JOY_BUTTON_B }, { "JOY_BUTTON_X", JOY_BUTTON_X }, { "JOY_BUTTON_Y", JOY_BUTTON_Y },
			{ "JOY_BUTTON_BACK", JOY_BUTTON_BACK }, { "JOY_BUTTON_GUIDE", JOY_BUTTON_GUIDE }, { "JOY_BUTTON_START", JOY_BUTTON_START },
			{ "JOY_BUTTON_LEFT_STICK", JOY_BUTTON_LEFT_STICK }, { "JOY_BUTTON_RIGHT_STICK", JOY_BUTTON_RIGHT_STICK },
			{ "JOY_BUTTON_LEFT_SHOULDER", JOY_BUTTON_LEFT_SHOULDER }, { "JOY_BUTTON_RIGHT_SHOULDER", JOY_BUTTON_RIGHT_SHOULDER },
			{ "JOY_BUTTON_DPAD_UP", JOY_BUTTON_DPAD_UP }, { "JOY_BUTTON_DPAD_DOWN", JOY_BUTTON_DPAD_DOWN },
			{ "JOY_BUTTON_DPAD_LEFT", JOY_BUTTON_DPAD_LEFT }, { "JOY_BUTTON_DPAD_RIGHT", JOY_BUTTON_DPAD_RIGHT },
			{ "JOY_AXIS_LEFT_X", JOY_AXIS_LEFT_X }, { "JOY_AXIS_LEFT_Y", JOY_AXIS_LEFT_Y }, { "JOY_AXIS_RIGHT_X", JOY_AXIS_RIGHT_X },
			{ "JOY_AXIS_RIGHT_Y", JOY_AXIS_RIGHT_Y }, { "JOY_AXIS_TRIGGER_LEFT", JOY_AXIS_TRIGGER_LEFT }, { "JOY_AXIS_TRIGGER_RIGHT", JOY_AXIS_TRIGGER_RIGHT },
		};
		for (const auto &kv : joy_constants) {
			constants[kv.first] = Variant((int64_t)kv.second);
		}
		for (int t = 0; t < Variant::VARIANT_MAX; t++) {
			constants["TYPE_" + str_to_upper(Variant::get_type_name((Variant::Type)t))] = Variant((int64_t)t);
		}
	}
	auto it = constants.find(p_name);
	if (it == constants.end()) {
		return false;
	}
	r_value = it->second;
	return true;
}
