#include "variant_parser.h"

#include "core/string/ustring.h"

typedef VariantParser::Stream Stream;

static void _skip_ws(Stream &s) {
	while (s.pos < s.text.size()) {
		char c = s.text[s.pos];
		if (c == '\n') {
			s.line++;
			s.pos++;
		} else if (c == ' ' || c == '\t' || c == '\r') {
			s.pos++;
		} else if (c == ';') { // commentaire jusqu'à la fin de ligne
			while (s.pos < s.text.size() && s.text[s.pos] != '\n') {
				s.pos++;
			}
		} else {
			break;
		}
	}
}

static bool _is_ident_char(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static String _read_ident(Stream &s) {
	size_t start = s.pos;
	while (s.pos < s.text.size() && _is_ident_char(s.text[s.pos])) {
		s.pos++;
	}
	return s.text.substr(start, s.pos - start);
}

static Error _parse_string(Stream &s, String &r_str, String &r_err) {
	s.pos++; // guillemet ouvrant
	r_str.clear();
	while (s.pos < s.text.size()) {
		char c = s.text[s.pos++];
		if (c == '"') {
			return OK;
		}
		if (c == '\\' && s.pos < s.text.size()) {
			char e = s.text[s.pos++];
			switch (e) {
				case 'n': r_str += '\n'; break;
				case 't': r_str += '\t'; break;
				case 'r': r_str += '\r'; break;
				default: r_str += e; break;
			}
			continue;
		}
		if (c == '\n') {
			s.line++;
		}
		r_str += c;
	}
	r_err = "Chaîne non terminée";
	return ERR_PARSE_ERROR;
}

static Error _parse_args(Stream &s, std::vector<Variant> &r_args, String &r_err, const VariantParser::ResourceResolver *p_resolver) {
	// On est juste après '('.
	_skip_ws(s);
	if (s.pos < s.text.size() && s.text[s.pos] == ')') {
		s.pos++;
		return OK;
	}
	while (true) {
		Variant v;
		Error err = VariantParser::parse_value(s, v, r_err, p_resolver);
		if (err != OK) {
			return err;
		}
		r_args.push_back(v);
		_skip_ws(s);
		if (s.pos >= s.text.size()) {
			r_err = "')' attendue";
			return ERR_PARSE_ERROR;
		}
		char c = s.text[s.pos++];
		if (c == ')') {
			return OK;
		}
		if (c != ',') {
			r_err = "',' ou ')' attendue";
			return ERR_PARSE_ERROR;
		}
	}
}

Error VariantParser::parse_value(Stream &s, Variant &r_value, String &r_err, const ResourceResolver *p_resolver) {
	_skip_ws(s);
	if (s.pos >= s.text.size()) {
		r_err = "Valeur attendue (fin de fichier)";
		return ERR_PARSE_ERROR;
	}
	char c = s.text[s.pos];

	if (c == '"') {
		String str;
		Error err = _parse_string(s, str, r_err);
		r_value = Variant(str);
		return err;
	}

	if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
		size_t start = s.pos;
		bool is_float = false;
		s.pos++;
		if (c == '-' && s.text.compare(s.pos, 3, "inf") == 0) {
			s.pos += 3;
			r_value = Variant(-INFINITY);
			return OK;
		}
		while (s.pos < s.text.size()) {
			char d = s.text[s.pos];
			if (d >= '0' && d <= '9') {
				s.pos++;
			} else if (d == '.' || d == 'e' || d == 'E') {
				is_float = true;
				s.pos++;
				if ((d == 'e' || d == 'E') && s.pos < s.text.size() && (s.text[s.pos] == '-' || s.text[s.pos] == '+')) {
					s.pos++;
				}
			} else {
				break;
			}
		}
		String num = s.text.substr(start, s.pos - start);
		if (is_float) {
			r_value = Variant(strtod(num.c_str(), nullptr));
		} else {
			r_value = Variant((int64_t)strtoll(num.c_str(), nullptr, 10));
		}
		return OK;
	}

	if (c == '[') {
		s.pos++;
		Array arr;
		_skip_ws(s);
		if (s.pos < s.text.size() && s.text[s.pos] == ']') {
			s.pos++;
			r_value = Variant(arr);
			return OK;
		}
		while (true) {
			Variant v;
			Error err = parse_value(s, v, r_err, p_resolver);
			if (err != OK) {
				return err;
			}
			arr.push_back(v);
			_skip_ws(s);
			if (s.pos >= s.text.size()) {
				r_err = "']' attendu";
				return ERR_PARSE_ERROR;
			}
			char d = s.text[s.pos++];
			if (d == ']') {
				break;
			}
			if (d != ',') {
				r_err = "',' ou ']' attendu";
				return ERR_PARSE_ERROR;
			}
			_skip_ws(s);
			if (s.pos < s.text.size() && s.text[s.pos] == ']') { // virgule finale
				s.pos++;
				break;
			}
		}
		r_value = Variant(arr);
		return OK;
	}

	if (c == '{') {
		s.pos++;
		Dictionary dict;
		_skip_ws(s);
		if (s.pos < s.text.size() && s.text[s.pos] == '}') {
			s.pos++;
			r_value = Variant(dict);
			return OK;
		}
		while (true) {
			Variant k, v;
			Error err = parse_value(s, k, r_err, p_resolver);
			if (err != OK) {
				return err;
			}
			_skip_ws(s);
			if (s.pos >= s.text.size() || s.text[s.pos] != ':') {
				r_err = "':' attendu dans le dictionnaire";
				return ERR_PARSE_ERROR;
			}
			s.pos++;
			err = parse_value(s, v, r_err, p_resolver);
			if (err != OK) {
				return err;
			}
			dict[k] = v;
			_skip_ws(s);
			if (s.pos >= s.text.size()) {
				r_err = "'}' attendue";
				return ERR_PARSE_ERROR;
			}
			char d = s.text[s.pos++];
			if (d == '}') {
				break;
			}
			if (d != ',') {
				r_err = "',' ou '}' attendue";
				return ERR_PARSE_ERROR;
			}
			_skip_ws(s);
			if (s.pos < s.text.size() && s.text[s.pos] == '}') {
				s.pos++;
				break;
			}
		}
		r_value = Variant(dict);
		return OK;
	}

	if (_is_ident_char(c)) {
		String id = _read_ident(s);
		if (id == "true") {
			r_value = Variant(true);
			return OK;
		}
		if (id == "false") {
			r_value = Variant(false);
			return OK;
		}
		if (id == "null" || id == "nil") {
			r_value = Variant();
			return OK;
		}
		if (id == "inf") {
			r_value = Variant(INFINITY);
			return OK;
		}
		if (id == "nan") {
			r_value = Variant(NAN);
			return OK;
		}
		_skip_ws(s);
		if (s.pos >= s.text.size() || s.text[s.pos] != '(') {
			r_err = "Identifiant inattendu : '" + id + "'";
			return ERR_PARSE_ERROR;
		}
		s.pos++;
		std::vector<Variant> args;
		Error err = _parse_args(s, args, r_err, p_resolver);
		if (err != OK) {
			return err;
		}
		auto f = [&](int i) -> real_t { return (real_t)args[i].as_float(); };
		if (id == "Vector2" && args.size() == 2) {
			r_value = Variant(Vector2(f(0), f(1)));
			return OK;
		}
		if (id == "Vector3" && args.size() == 3) {
			r_value = Variant(Vector3(f(0), f(1), f(2)));
			return OK;
		}
		if (id == "Rect2" && args.size() == 4) {
			r_value = Variant(Rect2(f(0), f(1), f(2), f(3)));
			return OK;
		}
		if (id == "Color" && (args.size() == 3 || args.size() == 4)) {
			r_value = Variant(Color(f(0), f(1), f(2), args.size() == 4 ? f(3) : 1.0f));
			return OK;
		}
		if ((id == "ExtResource" || id == "SubResource") && args.size() == 1) {
			if (p_resolver && *p_resolver) {
				r_value = (*p_resolver)(id, args[0].as_string());
			} else {
				r_value = Variant();
			}
			return OK;
		}
		r_err = "Constructeur inconnu ou arguments invalides : '" + id + "'";
		return ERR_PARSE_ERROR;
	}

	r_err = String("Caractère inattendu : '") + c + "'";
	return ERR_PARSE_ERROR;
}

VariantParser::Item VariantParser::parse_next(Stream &s, Tag &r_tag, String &r_key, Variant &r_value, String &r_err, const ResourceResolver *p_resolver) {
	_skip_ws(s);
	if (s.pos >= s.text.size()) {
		return ITEM_EOF;
	}
	char c = s.text[s.pos];
	if (c == '[') {
		s.pos++;
		_skip_ws(s);
		r_tag.name = _read_ident(s);
<<<<<<< HEAD
		// Noms à points, comme les sections « [preset.0.options] » de export_presets.cfg.
		while (s.pos < s.text.size() && s.text[s.pos] == '.') {
			s.pos++;
			r_tag.name += "." + _read_ident(s);
		}
=======
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
		r_tag.fields.clear();
		if (r_tag.name.empty()) {
			r_err = "Nom de balise attendu après '['";
			return ITEM_ERROR;
		}
		while (true) {
			_skip_ws(s);
			if (s.pos >= s.text.size()) {
				r_err = "']' attendu pour fermer la balise";
				return ITEM_ERROR;
			}
			if (s.text[s.pos] == ']') {
				s.pos++;
				return ITEM_TAG;
			}
			String field = _read_ident(s);
			_skip_ws(s);
			if (field.empty() || s.pos >= s.text.size() || s.text[s.pos] != '=') {
				r_err = "'champ=valeur' attendu dans la balise [" + r_tag.name + "]";
				return ITEM_ERROR;
			}
			s.pos++;
			Variant v;
			if (parse_value(s, v, r_err, p_resolver) != OK) {
				return ITEM_ERROR;
			}
			r_tag.fields[field] = v;
		}
	}

	// Affectation : clé (peut contenir '/', '.') jusqu'à '='.
	size_t start = s.pos;
	while (s.pos < s.text.size() && s.text[s.pos] != '=' && s.text[s.pos] != '\n') {
		s.pos++;
	}
	if (s.pos >= s.text.size() || s.text[s.pos] != '=') {
		r_err = "'=' attendu";
		return ITEM_ERROR;
	}
	r_key = str_strip(s.text.substr(start, s.pos - start));
	s.pos++;
	if (parse_value(s, r_value, r_err, p_resolver) != OK) {
		return ITEM_ERROR;
	}
	return ITEM_ASSIGN;
}

static String _write_float(double v) {
	String s = rtos(v);
	if (s.find_first_of(".einf") == String::npos) {
		s += ".0";
	}
	return s;
}

String VariantWriter::write(const Variant &p_value, const ObjectEncoder *p_encoder) {
	switch (p_value.get_type()) {
		case Variant::NIL: return "null";
		case Variant::BOOL: return p_value.as_bool() ? "true" : "false";
		case Variant::INT: return itos(p_value.as_int());
		case Variant::FLOAT: return _write_float(p_value.as_float());
		case Variant::STRING: return "\"" + str_c_escape(p_value.as_string()) + "\"";
		case Variant::VECTOR2: {
			Vector2 v = p_value.as_vector2();
			return "Vector2(" + rtos(v.x) + ", " + rtos(v.y) + ")";
		}
		case Variant::VECTOR3: {
			Vector3 v = p_value.as_vector3();
			return "Vector3(" + rtos(v.x) + ", " + rtos(v.y) + ", " + rtos(v.z) + ")";
		}
		case Variant::RECT2: {
			Rect2 r = p_value.as_rect2();
			return "Rect2(" + rtos(r.position.x) + ", " + rtos(r.position.y) + ", " + rtos(r.size.x) + ", " + rtos(r.size.y) + ")";
		}
		case Variant::COLOR: {
			Color c = p_value.as_color();
			return "Color(" + rtos(c.r) + ", " + rtos(c.g) + ", " + rtos(c.b) + ", " + rtos(c.a) + ")";
		}
		case Variant::OBJECT: {
			if (p_value.is_null() || !p_encoder || !*p_encoder) {
				return "null";
			}
			return (*p_encoder)(p_value);
		}
		case Variant::ARRAY: {
			Array a = p_value.as_array();
			String s = "[";
			for (int i = 0; i < a.size(); i++) {
				if (i) {
					s += ", ";
				}
				s += write(a[i], p_encoder);
			}
			return s + "]";
		}
		case Variant::DICTIONARY: {
			Dictionary d = p_value.as_dictionary();
			String s = "{";
			bool first = true;
			for (auto &kv : d.map()) {
				if (!first) {
					s += ", ";
				}
				first = false;
				s += write(kv.first, p_encoder) + ": " + write(kv.second, p_encoder);
			}
			return s + "}";
		}
		default: return "null";
	}
}
