// Opérateurs, accès aux membres et méthodes intégrées des Variants.
#include "core/object/object.h"
#include "core/string/ustring.h"
#include "variant.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Formatage "%d points" % valeur
// ---------------------------------------------------------------------------
static String _sprintf(const String &p_fmt, const Array &p_args, bool &r_valid) {
	String out;
	int arg_idx = 0;
	r_valid = true;
	for (size_t i = 0; i < p_fmt.size(); i++) {
		char c = p_fmt[i];
		if (c != '%') {
			out += c;
			continue;
		}
		i++;
		if (i >= p_fmt.size()) {
			r_valid = false;
			return out;
		}
		if (p_fmt[i] == '%') {
			out += '%';
			continue;
		}
		String spec = "%";
		while (i < p_fmt.size() && strchr("-+ 0#", p_fmt[i])) {
			spec += p_fmt[i++];
		}
		while (i < p_fmt.size() && ((p_fmt[i] >= '0' && p_fmt[i] <= '9') || p_fmt[i] == '.')) {
			spec += p_fmt[i++];
		}
		if (i >= p_fmt.size() || arg_idx >= p_args.size()) {
			r_valid = false;
			return out;
		}
		const Variant &arg = p_args[arg_idx++];
		switch (p_fmt[i]) {
			case 's':
				out += vformat((spec + "s").c_str(), arg.as_string().c_str());
				break;
			case 'd':
			case 'i':
				out += vformat((spec + "lld").c_str(), (long long)arg.as_int());
				break;
			case 'x':
				out += vformat((spec + "llx").c_str(), (long long)arg.as_int());
				break;
			case 'f':
				out += vformat((spec + "f").c_str(), arg.as_float());
				break;
			case 'c':
				out += arg.get_type() == Variant::STRING ? arg.as_string() : utf8_encode((uint32_t)arg.as_int());
				break;
			default:
				r_valid = false;
				return out;
		}
	}
	return out;
}

String Variant::get_operator_name(Operator p_op) {
	static const char *names[] = { "+", "-", "*", "/", "%", "-", "==", "!=", "<", "<=", ">", ">=", "in" };
	return names[p_op];
}

void Variant::evaluate(Operator p_op, const Variant &p_a, const Variant &p_b, Variant &r_ret, bool &r_valid) {
	r_valid = true;
	const Type ta = p_a.get_type(), tb = p_b.get_type();
	const bool both_num = p_a.is_num() && p_b.is_num();
	const bool both_int = ta == INT && tb == INT;

	switch (p_op) {
		case OP_EQ:
			r_ret = Variant(p_a == p_b);
			return;
		case OP_NE:
			r_ret = Variant(p_a != p_b);
			return;
		case OP_LT:
		case OP_LE:
		case OP_GT:
		case OP_GE: {
			int cmp;
			if (both_num) {
				double a = p_a.as_float(), b = p_b.as_float();
				cmp = a < b ? -1 : (a > b ? 1 : 0);
			} else if (ta == STRING && tb == STRING) {
				int c = p_a.as_string().compare(p_b.as_string());
				cmp = c < 0 ? -1 : (c > 0 ? 1 : 0);
			} else {
				r_valid = false;
				return;
			}
			bool res = p_op == OP_LT ? cmp < 0 : (p_op == OP_LE ? cmp <= 0 : (p_op == OP_GT ? cmp > 0 : cmp >= 0));
			r_ret = Variant(res);
			return;
		}
		case OP_IN:
			if (tb == ARRAY) {
				r_ret = Variant(p_b.as_array().has(p_a));
			} else if (tb == DICTIONARY) {
				r_ret = Variant(p_b.as_dictionary().has(p_a));
			} else if (tb == STRING && ta == STRING) {
				r_ret = Variant(p_b.as_string().find(p_a.as_string()) != String::npos);
			} else {
				r_valid = false;
			}
			return;
		case OP_NEG:
			if (ta == INT) {
				r_ret = Variant(-p_a.as_int());
			} else if (ta == FLOAT) {
				r_ret = Variant(-p_a.as_float());
			} else if (ta == VECTOR2) {
				r_ret = Variant(-p_a.as_vector2());
			} else if (ta == VECTOR3) {
				r_ret = Variant(-p_a.as_vector3());
			} else {
				r_valid = false;
			}
			return;
		case OP_ADD:
			if (both_int) {
				r_ret = Variant(p_a.as_int() + p_b.as_int());
			} else if (both_num) {
				r_ret = Variant(p_a.as_float() + p_b.as_float());
			} else if (ta == STRING || tb == STRING) {
				r_ret = Variant(p_a.as_string() + p_b.as_string());
			} else if (ta == VECTOR2 && tb == VECTOR2) {
				r_ret = Variant(p_a.as_vector2() + p_b.as_vector2());
			} else if (ta == VECTOR3 && tb == VECTOR3) {
				r_ret = Variant(p_a.as_vector3() + p_b.as_vector3());
			} else if (ta == COLOR && tb == COLOR) {
				r_ret = Variant(p_a.as_color() + p_b.as_color());
			} else if (ta == ARRAY && tb == ARRAY) {
				Array out = p_a.as_array().duplicate();
				for (const Variant &v : p_b.as_array().vec()) {
					out.push_back(v);
				}
				r_ret = Variant(out);
			} else {
				r_valid = false;
			}
			return;
		case OP_SUB:
			if (both_int) {
				r_ret = Variant(p_a.as_int() - p_b.as_int());
			} else if (both_num) {
				r_ret = Variant(p_a.as_float() - p_b.as_float());
			} else if (ta == VECTOR2 && tb == VECTOR2) {
				r_ret = Variant(p_a.as_vector2() - p_b.as_vector2());
			} else if (ta == VECTOR3 && tb == VECTOR3) {
				r_ret = Variant(p_a.as_vector3() - p_b.as_vector3());
			} else if (ta == COLOR && tb == COLOR) {
				r_ret = Variant(p_a.as_color() - p_b.as_color());
			} else {
				r_valid = false;
			}
			return;
		case OP_MUL:
			if (both_int) {
				r_ret = Variant(p_a.as_int() * p_b.as_int());
			} else if (both_num) {
				r_ret = Variant(p_a.as_float() * p_b.as_float());
			} else if (ta == VECTOR2 && tb == VECTOR2) {
				r_ret = Variant(p_a.as_vector2() * p_b.as_vector2());
			} else if (ta == VECTOR3 && tb == VECTOR3) {
				r_ret = Variant(p_a.as_vector3() * p_b.as_vector3());
			} else if (ta == VECTOR3 && p_b.is_num()) {
				r_ret = Variant(p_a.as_vector3() * (real_t)p_b.as_float());
			} else if (p_a.is_num() && tb == VECTOR3) {
				r_ret = Variant(p_b.as_vector3() * (real_t)p_a.as_float());
			} else if (ta == VECTOR2 && p_b.is_num()) {
				r_ret = Variant(p_a.as_vector2() * (real_t)p_b.as_float());
			} else if (p_a.is_num() && tb == VECTOR2) {
				r_ret = Variant(p_b.as_vector2() * (real_t)p_a.as_float());
			} else if (ta == COLOR && tb == COLOR) {
				r_ret = Variant(p_a.as_color() * p_b.as_color());
			} else if (ta == COLOR && p_b.is_num()) {
				r_ret = Variant(p_a.as_color() * (float)p_b.as_float());
			} else {
				r_valid = false;
			}
			return;
		case OP_DIV:
			if (both_int) {
				if (p_b.as_int() == 0) {
					r_valid = false;
					return;
				}
				r_ret = Variant(p_a.as_int() / p_b.as_int());
			} else if (both_num) {
				r_ret = Variant(p_a.as_float() / p_b.as_float());
			} else if (ta == VECTOR2 && tb == VECTOR2) {
				r_ret = Variant(p_a.as_vector2() / p_b.as_vector2());
			} else if (ta == VECTOR3 && tb == VECTOR3) {
				r_ret = Variant(p_a.as_vector3() / p_b.as_vector3());
			} else if (ta == VECTOR3 && p_b.is_num()) {
				r_ret = Variant(p_a.as_vector3() / (real_t)p_b.as_float());
			} else if (ta == VECTOR2 && p_b.is_num()) {
				r_ret = Variant(p_a.as_vector2() / (real_t)p_b.as_float());
			} else {
				r_valid = false;
			}
			return;
		case OP_MOD:
			if (both_int) {
				if (p_b.as_int() == 0) {
					r_valid = false;
					return;
				}
				r_ret = Variant(p_a.as_int() % p_b.as_int());
			} else if (both_num) {
				r_ret = Variant(std::fmod(p_a.as_float(), p_b.as_float()));
			} else if (ta == STRING) {
				Array args;
				if (tb == ARRAY) {
					args = p_b.as_array();
				} else {
					args.push_back(p_b);
				}
				r_ret = Variant(_sprintf(p_a.as_string(), args, r_valid));
			} else {
				r_valid = false;
			}
			return;
	}
	r_valid = false;
}

// ---------------------------------------------------------------------------
// Accès nommés et indexés
// ---------------------------------------------------------------------------
Variant Variant::get_named(const String &p_name, bool &r_valid) const {
	r_valid = true;
	switch (get_type()) {
		case VECTOR2: {
			const Vector2 &v = std::get<Vector2>(data);
			if (p_name == "x") {
				return Variant(v.x);
			}
			if (p_name == "y") {
				return Variant(v.y);
			}
		} break;
		case VECTOR3: {
			const Vector3 &v = std::get<Vector3>(data);
			if (p_name == "x") {
				return Variant(v.x);
			}
			if (p_name == "y") {
				return Variant(v.y);
			}
			if (p_name == "z") {
				return Variant(v.z);
			}
		} break;
		case RECT2: {
			const Rect2 &r = std::get<Rect2>(data);
			if (p_name == "position") {
				return Variant(r.position);
			}
			if (p_name == "size") {
				return Variant(r.size);
			}
			if (p_name == "end") {
				return Variant(r.get_end());
			}
		} break;
		case COLOR: {
			const Color &c = std::get<Color>(data);
			if (p_name == "r") {
				return Variant(c.r);
			}
			if (p_name == "g") {
				return Variant(c.g);
			}
			if (p_name == "b") {
				return Variant(c.b);
			}
			if (p_name == "a") {
				return Variant(c.a);
			}
		} break;
		case OBJECT: {
			Object *obj = as_object();
			if (obj) {
				bool valid = false;
				Variant ret = obj->get(p_name, &valid);
				if (valid) {
					return ret;
				}
				if (obj->has_signal(p_name)) {
					return Variant(Signal(obj, p_name));
				}
				if (obj->has_method(p_name)) {
					return Variant(Callable(obj, p_name));
				}
			}
		} break;
		case DICTIONARY: {
			const Dictionary &d = std::get<Dictionary>(data);
			if (d.has(Variant(p_name))) {
				return d.get(Variant(p_name), Variant());
			}
		} break;
		default:
			break;
	}
	r_valid = false;
	return Variant();
}

void Variant::set_named(const String &p_name, const Variant &p_value, bool &r_valid) {
	r_valid = true;
	switch (get_type()) {
		case VECTOR2: {
			Vector2 &v = std::get<Vector2>(data);
			if (!p_value.is_num()) {
				break;
			}
			if (p_name == "x") {
				v.x = (real_t)p_value.as_float();
				return;
			}
			if (p_name == "y") {
				v.y = (real_t)p_value.as_float();
				return;
			}
		} break;
		case VECTOR3: {
			Vector3 &v = std::get<Vector3>(data);
			if (!p_value.is_num()) {
				break;
			}
			if (p_name == "x") {
				v.x = (real_t)p_value.as_float();
				return;
			}
			if (p_name == "y") {
				v.y = (real_t)p_value.as_float();
				return;
			}
			if (p_name == "z") {
				v.z = (real_t)p_value.as_float();
				return;
			}
		} break;
		case RECT2: {
			Rect2 &r = std::get<Rect2>(data);
			if (p_value.get_type() != VECTOR2) {
				break;
			}
			if (p_name == "position") {
				r.position = p_value.as_vector2();
				return;
			}
			if (p_name == "size") {
				r.size = p_value.as_vector2();
				return;
			}
			if (p_name == "end") {
				r.size = p_value.as_vector2() - r.position;
				return;
			}
		} break;
		case COLOR: {
			Color &c = std::get<Color>(data);
			if (!p_value.is_num()) {
				break;
			}
			float f = (float)p_value.as_float();
			if (p_name == "r") {
				c.r = f;
				return;
			}
			if (p_name == "g") {
				c.g = f;
				return;
			}
			if (p_name == "b") {
				c.b = f;
				return;
			}
			if (p_name == "a") {
				c.a = f;
				return;
			}
		} break;
		case OBJECT: {
			Object *obj = as_object();
			if (obj) {
				bool valid = false;
				obj->set(p_name, p_value, &valid);
				if (valid) {
					return;
				}
			}
		} break;
		case DICTIONARY: {
			std::get<Dictionary>(data)[Variant(p_name)] = p_value;
			return;
		}
		default:
			break;
	}
	r_valid = false;
}

Variant Variant::get_indexed(const Variant &p_index, bool &r_valid) const {
	r_valid = true;
	switch (get_type()) {
		case ARRAY: {
			const Array &a = std::get<Array>(data);
			if (!p_index.is_num()) {
				break;
			}
			int64_t idx = p_index.as_int();
			if (idx < 0) {
				idx += a.size();
			}
			if (idx < 0 || idx >= a.size()) {
				break;
			}
			return a[(int)idx];
		}
		case DICTIONARY: {
			const Dictionary &d = std::get<Dictionary>(data);
			if (!d.has(p_index)) {
				break;
			}
			return d.get(p_index, Variant());
		}
		case STRING: {
			if (!p_index.is_num()) {
				break;
			}
			std::vector<uint32_t> cps = utf8_decode(std::get<String>(data));
			int64_t idx = p_index.as_int();
			if (idx < 0) {
				idx += (int64_t)cps.size();
			}
			if (idx < 0 || idx >= (int64_t)cps.size()) {
				break;
			}
			return Variant(utf8_encode(cps[idx]));
		}
		case VECTOR3: {
			if (!p_index.is_num()) {
				break;
			}
			int64_t idx = p_index.as_int();
			if (idx >= 0 && idx < 3) {
				return Variant(std::get<Vector3>(data)[(int)idx]);
			}
		} break;
		case VECTOR2: {
			if (!p_index.is_num()) {
				break;
			}
			int64_t idx = p_index.as_int();
			const Vector2 &v = std::get<Vector2>(data);
			if (idx == 0) {
				return Variant(v.x);
			}
			if (idx == 1) {
				return Variant(v.y);
			}
		} break;
		case OBJECT:
			if (p_index.get_type() == STRING) {
				return get_named(p_index.as_string(), r_valid);
			}
			break;
		default:
			break;
	}
	r_valid = false;
	return Variant();
}

void Variant::set_indexed(const Variant &p_index, const Variant &p_value, bool &r_valid) {
	r_valid = true;
	switch (get_type()) {
		case ARRAY: {
			Array &a = std::get<Array>(data);
			if (!p_index.is_num()) {
				break;
			}
			int64_t idx = p_index.as_int();
			if (idx < 0) {
				idx += a.size();
			}
			if (idx < 0 || idx >= a.size()) {
				break;
			}
			a[(int)idx] = p_value;
			return;
		}
		case DICTIONARY:
			std::get<Dictionary>(data)[p_index] = p_value;
			return;
		case VECTOR3: {
			if (!p_index.is_num() || !p_value.is_num()) {
				break;
			}
			int64_t idx = p_index.as_int();
			if (idx >= 0 && idx < 3) {
				std::get<Vector3>(data)[(int)idx] = (real_t)p_value.as_float();
				return;
			}
		} break;
		case VECTOR2: {
			if (!p_index.is_num() || !p_value.is_num()) {
				break;
			}
			int64_t idx = p_index.as_int();
			Vector2 &v = std::get<Vector2>(data);
			if (idx == 0) {
				v.x = (real_t)p_value.as_float();
				return;
			}
			if (idx == 1) {
				v.y = (real_t)p_value.as_float();
				return;
			}
		} break;
		case OBJECT:
			if (p_index.get_type() == STRING) {
				set_named(p_index.as_string(), p_value, r_valid);
				return;
			}
			break;
		default:
			break;
	}
	r_valid = false;
}

// ---------------------------------------------------------------------------
// Méthodes intégrées
// ---------------------------------------------------------------------------
#define ARGC(m_min, m_max)                                         \
	if (p_argc < (m_min)) {                                        \
		r_error.error = CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;   \
		r_error.expected = (m_min);                                \
		return Variant();                                          \
	}                                                              \
	if (p_argc > (m_max)) {                                        \
		r_error.error = CallError::CALL_ERROR_TOO_MANY_ARGUMENTS;  \
		r_error.expected = (m_max);                                \
		return Variant();                                          \
	}
#define M(m_name, m_min, m_max) \
	if (p_method == m_name) {   \
		ARGC(m_min, m_max)
#define END }
#define A(i) (*p_args[i])

Variant Variant::call_method(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error) {
	r_error.error = CallError::CALL_OK;
	switch (get_type()) {
		case OBJECT: {
			Object *obj = as_object();
			if (!obj) {
				r_error.error = CallError::CALL_ERROR_INSTANCE_IS_NULL;
				return Variant();
			}
			return obj->callp(p_method, p_args, p_argc, r_error);
		}
		case STRING: {
			const String &s = std::get<String>(data);
			M("length", 0, 0) return Variant((int64_t)utf8_decode(s).size()); END
			M("is_empty", 0, 0) return Variant(s.empty()); END
			M("to_upper", 0, 0) return Variant(str_to_upper(s)); END
			M("to_lower", 0, 0) return Variant(str_to_lower(s)); END
			M("capitalize", 0, 0) return Variant(str_capitalize(s)); END
			M("strip_edges", 0, 0) return Variant(str_strip(s)); END
			M("begins_with", 1, 1) return Variant(str_begins_with(s, A(0).as_string())); END
			M("ends_with", 1, 1) return Variant(str_ends_with(s, A(0).as_string())); END
			M("contains", 1, 1) return Variant(s.find(A(0).as_string()) != String::npos); END
			M("find", 1, 1)
				size_t pos = s.find(A(0).as_string());
				return Variant(pos == String::npos ? (int64_t)-1 : (int64_t)pos);
			END
			M("substr", 1, 2)
				int64_t from = Math::clamp<int64_t>(A(0).as_int(), 0, (int64_t)s.size());
				int64_t len = p_argc > 1 ? A(1).as_int() : -1;
				return Variant(s.substr((size_t)from, len < 0 ? String::npos : (size_t)len));
			END
			M("left", 1, 1) return Variant(s.substr(0, (size_t)std::max<int64_t>(0, A(0).as_int()))); END
			M("right", 1, 1)
				size_t n = (size_t)std::max<int64_t>(0, A(0).as_int());
				return Variant(n >= s.size() ? s : s.substr(s.size() - n));
			END
			M("replace", 2, 2) return Variant(str_replace(s, A(0).as_string(), A(1).as_string())); END
			M("split", 1, 2)
				Array out;
				for (const String &part : str_split(s, A(0).as_string(), p_argc > 1 ? A(1).as_bool() : true)) {
					out.push_back(Variant(part));
				}
				return Variant(out);
			END
			M("repeat", 1, 1)
				String out;
				for (int64_t i = 0; i < A(0).as_int(); i++) {
					out += s;
				}
				return Variant(out);
			END
			M("pad_zeros", 1, 1)
				String out = s;
				while ((int64_t)out.size() < A(0).as_int()) {
					out = "0" + out;
				}
				return Variant(out);
			END
			M("to_int", 0, 0) return Variant(as_int()); END
			M("to_float", 0, 0) return Variant(as_float()); END
			M("get_extension", 0, 0) return Variant(path_get_extension(s)); END
			M("get_file", 0, 0) return Variant(path_get_file(s)); END
			M("get_basename", 0, 0) return Variant(path_get_basename(s)); END
			M("get_base_dir", 0, 0) return Variant(path_get_base_dir(s)); END
			M("path_join", 1, 1) return Variant(path_join(s, A(0).as_string())); END
		} break;
		case VECTOR2: {
			const Vector2 &v = std::get<Vector2>(data);
			M("length", 0, 0) return Variant(v.length()); END
			M("length_squared", 0, 0) return Variant(v.length_squared()); END
			M("normalized", 0, 0) return Variant(v.normalized()); END
			M("dot", 1, 1) return Variant(v.dot(A(0).as_vector2())); END
			M("cross", 1, 1) return Variant(v.cross(A(0).as_vector2())); END
			M("angle", 0, 0) return Variant(v.angle()); END
			M("angle_to", 1, 1) return Variant(v.angle_to(A(0).as_vector2())); END
			M("angle_to_point", 1, 1) return Variant(v.angle_to_point(A(0).as_vector2())); END
			M("distance_to", 1, 1) return Variant(v.distance_to(A(0).as_vector2())); END
			M("distance_squared_to", 1, 1) return Variant(v.distance_squared_to(A(0).as_vector2())); END
			M("direction_to", 1, 1) return Variant(v.direction_to(A(0).as_vector2())); END
			M("rotated", 1, 1) return Variant(v.rotated((real_t)A(0).as_float())); END
			M("lerp", 2, 2) return Variant(v.lerp(A(0).as_vector2(), (real_t)A(1).as_float())); END
			M("slide", 1, 1) return Variant(v.slide(A(0).as_vector2())); END
			M("bounce", 1, 1) return Variant(v.bounce(A(0).as_vector2())); END
			M("reflect", 1, 1) return Variant(v.reflect(A(0).as_vector2())); END
			M("abs", 0, 0) return Variant(v.abs()); END
			M("floor", 0, 0) return Variant(v.floor()); END
			M("round", 0, 0) return Variant(v.round()); END
			M("sign", 0, 0) return Variant(v.sign()); END
			M("orthogonal", 0, 0) return Variant(v.orthogonal()); END
			M("limit_length", 0, 1) return Variant(v.limit_length(p_argc > 0 ? (real_t)A(0).as_float() : 1.0f)); END
			M("move_toward", 2, 2) return Variant(v.move_toward(A(0).as_vector2(), (real_t)A(1).as_float())); END
			M("clamp", 2, 2) return Variant(v.clamp(A(0).as_vector2(), A(1).as_vector2())); END
			M("is_zero_approx", 0, 0) return Variant(v.is_zero_approx()); END
			M("is_equal_approx", 1, 1) return Variant(v.is_equal_approx(A(0).as_vector2())); END
		} break;
		case VECTOR3: {
			const Vector3 &v = std::get<Vector3>(data);
			M("length", 0, 0) return Variant(v.length()); END
			M("length_squared", 0, 0) return Variant(v.length_squared()); END
			M("normalized", 0, 0) return Variant(v.normalized()); END
			M("dot", 1, 1) return Variant(v.dot(A(0).as_vector3())); END
			M("cross", 1, 1) return Variant(v.cross(A(0).as_vector3())); END
			M("angle_to", 1, 1) return Variant(v.angle_to(A(0).as_vector3())); END
			M("distance_to", 1, 1) return Variant(v.distance_to(A(0).as_vector3())); END
			M("distance_squared_to", 1, 1) return Variant(v.distance_squared_to(A(0).as_vector3())); END
			M("direction_to", 1, 1) return Variant(v.direction_to(A(0).as_vector3())); END
			M("rotated", 2, 2) return Variant(v.rotated(A(0).as_vector3().normalized(), (real_t)A(1).as_float())); END
			M("lerp", 2, 2) return Variant(v.lerp(A(0).as_vector3(), (real_t)A(1).as_float())); END
			M("slide", 1, 1) return Variant(v.slide(A(0).as_vector3())); END
			M("bounce", 1, 1) return Variant(v.bounce(A(0).as_vector3())); END
			M("reflect", 1, 1) return Variant(v.reflect(A(0).as_vector3())); END
			M("project", 1, 1) return Variant(v.project(A(0).as_vector3())); END
			M("abs", 0, 0) return Variant(v.abs()); END
			M("floor", 0, 0) return Variant(v.floor()); END
			M("round", 0, 0) return Variant(v.round()); END
			M("sign", 0, 0) return Variant(v.sign()); END
			M("limit_length", 0, 1) return Variant(v.limit_length(p_argc > 0 ? (real_t)A(0).as_float() : 1.0f)); END
			M("move_toward", 2, 2) return Variant(v.move_toward(A(0).as_vector3(), (real_t)A(1).as_float())); END
			M("clamp", 2, 2) return Variant(v.clamp(A(0).as_vector3(), A(1).as_vector3())); END
			M("is_zero_approx", 0, 0) return Variant(v.is_zero_approx()); END
			M("is_equal_approx", 1, 1) return Variant(v.is_equal_approx(A(0).as_vector3())); END
		} break;
		case RECT2: {
			const Rect2 &r = std::get<Rect2>(data);
			M("has_point", 1, 1) return Variant(r.has_point(A(0).as_vector2())); END
			M("intersects", 1, 1) return Variant(r.intersects(A(0).as_rect2())); END
			M("encloses", 1, 1) return Variant(r.encloses(A(0).as_rect2())); END
			M("merge", 1, 1) return Variant(r.merge(A(0).as_rect2())); END
			M("intersection", 1, 1) return Variant(r.intersection(A(0).as_rect2())); END
			M("grow", 1, 1) return Variant(r.grow((real_t)A(0).as_float())); END
			M("expand", 1, 1) return Variant(r.expand(A(0).as_vector2())); END
			M("abs", 0, 0) return Variant(r.abs()); END
			M("get_center", 0, 0) return Variant(r.get_center()); END
			M("get_area", 0, 0) return Variant(r.get_area()); END
			M("has_area", 0, 0) return Variant(r.has_area()); END
		} break;
		case COLOR: {
			const Color &c = std::get<Color>(data);
			M("lerp", 2, 2) return Variant(c.lerp(A(0).as_color(), (float)A(1).as_float())); END
			M("darkened", 1, 1) return Variant(c.darkened((float)A(0).as_float())); END
			M("lightened", 1, 1) return Variant(c.lightened((float)A(0).as_float())); END
			M("to_html", 0, 1) return Variant(c.to_html(p_argc > 0 ? A(0).as_bool() : true)); END
		} break;
		case ARRAY: {
			Array &a = std::get<Array>(data);
			M("size", 0, 0) return Variant(a.size()); END
			M("is_empty", 0, 0) return Variant(a.is_empty()); END
			M("append", 1, 1) a.push_back(A(0)); return Variant(); END
			M("push_back", 1, 1) a.push_back(A(0)); return Variant(); END
			M("push_front", 1, 1) a.insert(0, A(0)); return Variant(); END
			M("append_array", 1, 1)
				Array other = A(0).as_array().duplicate();
				for (const Variant &v : other.vec()) {
					a.push_back(v);
				}
				return Variant();
			END
			M("pop_back", 0, 0) return a.pop_back(); END
			M("pop_front", 0, 0) return a.pop_front(); END
			M("insert", 2, 2) a.insert((int)A(0).as_int(), A(1)); return Variant(); END
			M("remove_at", 1, 1) a.remove_at((int)A(0).as_int()); return Variant(); END
			M("erase", 1, 1) a.erase(A(0)); return Variant(); END
			M("clear", 0, 0) a.clear(); return Variant(); END
			M("has", 1, 1) return Variant(a.has(A(0))); END
			M("find", 1, 1) return Variant(a.find(A(0))); END
			M("count", 1, 1)
				int n = 0;
				for (const Variant &v : a.vec()) {
					n += (v == A(0)) ? 1 : 0;
				}
				return Variant(n);
			END
			M("front", 0, 0) return a.is_empty() ? Variant() : a[0]; END
			M("back", 0, 0) return a.is_empty() ? Variant() : a[a.size() - 1]; END
			M("duplicate", 0, 1) return Variant(a.duplicate()); END
			M("resize", 1, 1) a.resize((int)A(0).as_int()); return Variant(); END
			M("reverse", 0, 0) std::reverse(a.vec().begin(), a.vec().end()); return Variant(); END
			M("sort", 0, 0) std::stable_sort(a.vec().begin(), a.vec().end()); return Variant(); END
			M("shuffle", 0, 0)
				for (int i = a.size() - 1; i > 0; i--) {
					int j = (int)Math::randi_range(0, i);
					std::swap(a[i], a[j]);
				}
				return Variant();
			END
			M("pick_random", 0, 0) return a.is_empty() ? Variant() : a[(int)Math::randi_range(0, a.size() - 1)]; END
			M("map", 1, 1)
				Callable fn = A(0).as_callable();
				Array src = a.duplicate(); // le tableau peut être modifié par la fonction
				Array out;
				for (const Variant &v : src.vec()) {
					const Variant *argp[1] = { &v };
					CallError ce;
					out.push_back(fn.callp(argp, 1, ce));
				}
				return Variant(out);
			END
			M("filter", 1, 1)
				Callable fn = A(0).as_callable();
				Array src = a.duplicate();
				Array out;
				for (const Variant &v : src.vec()) {
					const Variant *argp[1] = { &v };
					CallError ce;
					if (fn.callp(argp, 1, ce).booleanize()) {
						out.push_back(v);
					}
				}
				return Variant(out);
			END
			M("any", 1, 1)
				Callable fn = A(0).as_callable();
				Array src = a.duplicate();
				for (const Variant &v : src.vec()) {
					const Variant *argp[1] = { &v };
					CallError ce;
					if (fn.callp(argp, 1, ce).booleanize()) {
						return Variant(true);
					}
				}
				return Variant(false);
			END
			M("all", 1, 1)
				Callable fn = A(0).as_callable();
				Array src = a.duplicate();
				for (const Variant &v : src.vec()) {
					const Variant *argp[1] = { &v };
					CallError ce;
					if (!fn.callp(argp, 1, ce).booleanize()) {
						return Variant(false);
					}
				}
				return Variant(true);
			END
			M("reduce", 1, 2)
				Callable fn = A(0).as_callable();
				Array src = a.duplicate();
				Variant acc = p_argc > 1 ? A(1) : Variant();
				bool first = p_argc < 2;
				for (const Variant &v : src.vec()) {
					if (first) {
						acc = v;
						first = false;
						continue;
					}
					const Variant *argp[2] = { &acc, &v };
					CallError ce;
					Variant next = fn.callp(argp, 2, ce);
					acc = next;
				}
				return acc;
			END
			M("sort_custom", 1, 1)
				Callable fn = A(0).as_callable();
				std::vector<Variant> items = a.vec();
				// Tri par insertion : reste correct même si la comparaison est incohérente.
				for (size_t i = 1; i < items.size(); i++) {
					Variant key = items[i];
					size_t j = i;
					while (j > 0) {
						const Variant *argp[2] = { &key, &items[j - 1] };
						CallError ce;
						if (!fn.callp(argp, 2, ce).booleanize()) {
							break;
						}
						items[j] = items[j - 1];
						j--;
					}
					items[j] = key;
				}
				a.vec() = items;
				return Variant();
			END
			M("min", 0, 0)
				if (a.is_empty()) {
					return Variant();
				}
				return *std::min_element(a.vec().begin(), a.vec().end());
			END
			M("max", 0, 0)
				if (a.is_empty()) {
					return Variant();
				}
				return *std::max_element(a.vec().begin(), a.vec().end());
			END
			M("join", 0, 1)
				String sep = p_argc > 0 ? A(0).as_string() : String();
				String out;
				for (int i = 0; i < a.size(); i++) {
					if (i > 0) {
						out += sep;
					}
					out += a[i].stringify();
				}
				return Variant(out);
			END
			M("slice", 1, 2)
				int n = a.size();
				int64_t b = A(0).as_int(), e = p_argc > 1 ? A(1).as_int() : n;
				if (b < 0) {
					b += n;
				}
				if (e < 0) {
					e += n;
				}
				b = Math::clamp<int64_t>(b, 0, n);
				e = Math::clamp<int64_t>(e, 0, n);
				Array out;
				for (int64_t i = b; i < e; i++) {
					out.push_back(a[(int)i]);
				}
				return Variant(out);
			END
		} break;
		case DICTIONARY: {
			Dictionary &d = std::get<Dictionary>(data);
			M("size", 0, 0) return Variant(d.size()); END
			M("is_empty", 0, 0) return Variant(d.is_empty()); END
			M("has", 1, 1) return Variant(d.has(A(0))); END
			M("erase", 1, 1) d.erase(A(0)); return Variant(); END
			M("clear", 0, 0) d.clear(); return Variant(); END
			M("keys", 0, 0) return Variant(d.keys()); END
			M("values", 0, 0) return Variant(d.values()); END
			M("get", 1, 2) return d.get(A(0), p_argc > 1 ? A(1) : Variant()); END
			M("duplicate", 0, 1) return Variant(d.duplicate()); END
			M("merge", 1, 1)
				Dictionary other = A(0).as_dictionary();
				for (auto &kv : other.map()) {
					if (!d.has(kv.first)) {
						d[kv.first] = kv.second;
					}
				}
				return Variant();
			END
		} break;
		case CALLABLE: {
			const Callable &c = std::get<Callable>(data);
			if (p_method == "call") {
				return c.callp(p_args, p_argc, r_error);
			}
			M("is_valid", 0, 0) return Variant(c.is_valid()); END
			M("is_null", 0, 0) return Variant(c.is_null()); END
			M("get_object", 0, 0) return Variant(c.get_object()); END
			M("get_method", 0, 0) return Variant(c.method); END
		} break;
		case SIGNAL: {
			const Signal &s = std::get<Signal>(data);
			Object *obj = s.get_object();
			if (!obj) {
				r_error.error = CallError::CALL_ERROR_INSTANCE_IS_NULL;
				return Variant();
			}
			if (p_method == "emit") {
				obj->emit_signalp(s.name, p_args, p_argc);
				return Variant();
			}
			M("connect", 1, 2)
				obj->connect(s.name, A(0).as_callable(), p_argc > 1 ? (uint32_t)A(1).as_int() : 0);
				return Variant();
			END
			M("disconnect", 1, 1) obj->disconnect(s.name, A(0).as_callable()); return Variant(); END
			M("is_connected", 1, 1) return Variant(obj->is_connected(s.name, A(0).as_callable())); END
			M("get_name", 0, 0) return Variant(s.name); END
		} break;
		default:
			break;
	}
	r_error.error = CallError::CALL_ERROR_INVALID_METHOD;
	return Variant();
}
