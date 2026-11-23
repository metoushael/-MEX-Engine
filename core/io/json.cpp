#include "json.h"

#include "core/object/class_db.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

void _write_string(const String &p_text, String &r_out) {
	r_out += '"';
	for (const char raw : p_text) {
		const unsigned char c = (unsigned char)raw;
		switch (c) {
			case '"':
				r_out += "\\\"";
				break;
			case '\\':
				r_out += "\\\\";
				break;
			case '\n':
				r_out += "\\n";
				break;
			case '\r':
				r_out += "\\r";
				break;
			case '\t':
				r_out += "\\t";
				break;
			default:
				if (c < 0x20) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x", c);
					r_out += buffer;
				} else {
					r_out += raw; // UTF-8 laissé tel quel
				}
		}
	}
	r_out += '"';
}

void _write(const Variant &p_value, const String &p_indent, int p_depth, String &r_out) {
	const auto newline = [&](int p_level) {
		if (!p_indent.empty()) {
			r_out += '\n';
			for (int i = 0; i < p_level; i++) {
				r_out += p_indent;
			}
		}
	};
	switch (p_value.get_type()) {
		case Variant::NIL:
			r_out += "null";
			break;
		case Variant::BOOL:
			r_out += p_value.as_bool() ? "true" : "false";
			break;
		case Variant::INT:
			r_out += std::to_string(p_value.as_int());
			break;
		case Variant::FLOAT: {
			const double number = p_value.as_float();
			if (!std::isfinite(number)) {
				r_out += "null"; // JSON n'a ni infini ni NaN
			} else {
				char buffer[40];
				snprintf(buffer, sizeof(buffer), "%.15g", number);
				if (strtod(buffer, nullptr) != number) {
					snprintf(buffer, sizeof(buffer), "%.17g", number);
				}
				r_out += buffer;
			}
		} break;
		case Variant::ARRAY: {
			const Array array = p_value.as_array();
			if (array.size() == 0) {
				r_out += "[]";
				break;
			}
			r_out += '[';
			for (int i = 0; i < array.size(); i++) {
				if (i > 0) {
					r_out += ',';
				}
				newline(p_depth + 1);
				_write(array[i], p_indent, p_depth + 1, r_out);
			}
			newline(p_depth);
			r_out += ']';
		} break;
		case Variant::DICTIONARY: {
			const Dictionary dictionary = p_value.as_dictionary();
			if (dictionary.size() == 0) {
				r_out += "{}";
				break;
			}
			r_out += '{';
			bool first = true;
			for (const auto &entry : dictionary.map()) {
				if (!first) {
					r_out += ',';
				}
				first = false;
				newline(p_depth + 1);
				_write_string(entry.first.as_string(), r_out);
				r_out += p_indent.empty() ? ":" : ": ";
				_write(entry.second, p_indent, p_depth + 1, r_out);
			}
			newline(p_depth);
			r_out += '}';
		} break;
		default:
			_write_string(p_value.as_string(), r_out);
	}
}

struct Parser {
	const String &text;
	size_t pos = 0;
	String error;

	explicit Parser(const String &p_text) :
			text(p_text) {}

	bool fail(const String &p_message) {
		if (error.empty()) {
			error = p_message + " (caractère " + std::to_string(pos + 1) + ")";
		}
		return false;
	}

	void skip_space() {
		while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
			pos++;
		}
	}

	static void append_utf8(uint32_t p_code, String &r_out) {
		if (p_code < 0x80) {
			r_out += (char)p_code;
		} else if (p_code < 0x800) {
			r_out += (char)(0xC0 | (p_code >> 6));
			r_out += (char)(0x80 | (p_code & 0x3F));
		} else if (p_code < 0x10000) {
			r_out += (char)(0xE0 | (p_code >> 12));
			r_out += (char)(0x80 | ((p_code >> 6) & 0x3F));
			r_out += (char)(0x80 | (p_code & 0x3F));
		} else {
			r_out += (char)(0xF0 | (p_code >> 18));
			r_out += (char)(0x80 | ((p_code >> 12) & 0x3F));
			r_out += (char)(0x80 | ((p_code >> 6) & 0x3F));
			r_out += (char)(0x80 | (p_code & 0x3F));
		}
	}

	bool read_hex4(uint32_t &r_code) {
		if (pos + 4 > text.size()) {
			return fail("Séquence \\u incomplète");
		}
		r_code = 0;
		for (int i = 0; i < 4; i++) {
			const char c = text[pos++];
			r_code <<= 4;
			if (c >= '0' && c <= '9') {
				r_code |= (uint32_t)(c - '0');
			} else if (c >= 'a' && c <= 'f') {
				r_code |= (uint32_t)(c - 'a' + 10);
			} else if (c >= 'A' && c <= 'F') {
				r_code |= (uint32_t)(c - 'A' + 10);
			} else {
				return fail("Séquence \\u invalide");
			}
		}
		return true;
	}

	bool parse_string(String &r_out) {
		pos++; // guillemet ouvrant
		while (pos < text.size()) {
			const char c = text[pos++];
			if (c == '"') {
				return true;
			}
			if (c != '\\') {
				r_out += c;
				continue;
			}
			if (pos >= text.size()) {
				break;
			}
			const char escape = text[pos++];
			switch (escape) {
				case '"':
				case '\\':
				case '/':
					r_out += escape;
					break;
				case 'b':
					r_out += '\b';
					break;
				case 'f':
					r_out += '\f';
					break;
				case 'n':
					r_out += '\n';
					break;
				case 'r':
					r_out += '\r';
					break;
				case 't':
					r_out += '\t';
					break;
				case 'u': {
					uint32_t code = 0;
					if (!read_hex4(code)) {
						return false;
					}
					// Paire de substitution UTF-16 : deux \uXXXX pour un seul caractère.
					if (code >= 0xD800 && code <= 0xDBFF && pos + 1 < text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
						pos += 2;
						uint32_t low = 0;
						if (!read_hex4(low)) {
							return false;
						}
						if (low >= 0xDC00 && low <= 0xDFFF) {
							code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
						} else {
							append_utf8(0xFFFD, r_out);
							code = low;
						}
					}
					if (code >= 0xD800 && code <= 0xDFFF) {
						code = 0xFFFD; // moitié de paire isolée
					}
					append_utf8(code, r_out);
				} break;
				default:
					return fail("Échappement inconnu dans une chaîne");
			}
		}
		return fail("Chaîne non terminée");
	}

	bool parse_value(Variant &r_value, int p_depth) {
		if (p_depth > 200) {
			return fail("Imbrication trop profonde");
		}
		skip_space();
		if (pos >= text.size()) {
			return fail("Fin du texte inattendue");
		}
		const char c = text[pos];
		if (c == '{') {
			pos++;
			Dictionary dictionary;
			skip_space();
			if (pos < text.size() && text[pos] == '}') {
				pos++;
				r_value = Variant(dictionary);
				return true;
			}
			while (true) {
				skip_space();
				if (pos >= text.size() || text[pos] != '"') {
					return fail("Nom de champ attendu");
				}
				String key;
				if (!parse_string(key)) {
					return false;
				}
				skip_space();
				if (pos >= text.size() || text[pos] != ':') {
					return fail("':' attendu");
				}
				pos++;
				Variant value;
				if (!parse_value(value, p_depth + 1)) {
					return false;
				}
				dictionary[Variant(key)] = value;
				skip_space();
				if (pos < text.size() && text[pos] == ',') {
					pos++;
					continue;
				}
				if (pos < text.size() && text[pos] == '}') {
					pos++;
					break;
				}
				return fail("',' ou '}' attendu");
			}
			r_value = Variant(dictionary);
			return true;
		}
		if (c == '[') {
			pos++;
			Array array;
			skip_space();
			if (pos < text.size() && text[pos] == ']') {
				pos++;
				r_value = Variant(array);
				return true;
			}
			while (true) {
				Variant value;
				if (!parse_value(value, p_depth + 1)) {
					return false;
				}
				array.push_back(value);
				skip_space();
				if (pos < text.size() && text[pos] == ',') {
					pos++;
					continue;
				}
				if (pos < text.size() && text[pos] == ']') {
					pos++;
					break;
				}
				return fail("',' ou ']' attendu");
			}
			r_value = Variant(array);
			return true;
		}
		if (c == '"') {
			String value;
			if (!parse_string(value)) {
				return false;
			}
			r_value = Variant(value);
			return true;
		}
		if (text.compare(pos, 4, "true") == 0) {
			pos += 4;
			r_value = Variant(true);
			return true;
		}
		if (text.compare(pos, 5, "false") == 0) {
			pos += 5;
			r_value = Variant(false);
			return true;
		}
		if (text.compare(pos, 4, "null") == 0) {
			pos += 4;
			r_value = Variant();
			return true;
		}
		if (c == '-' || (c >= '0' && c <= '9')) {
			const size_t start = pos;
			bool is_float = false;
			if (text[pos] == '-') {
				pos++;
			}
			while (pos < text.size()) {
				const char d = text[pos];
				if (d >= '0' && d <= '9') {
					pos++;
				} else if (d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') {
					is_float = true;
					pos++;
				} else {
					break;
				}
			}
			const String number = text.substr(start, pos - start);
			if (number.empty() || number == "-") {
				return fail("Nombre invalide");
			}
			// Un entier trop long pour 64 bits devient un nombre à virgule.
			if (!is_float && number.size() <= 18) {
				r_value = Variant((int64_t)strtoll(number.c_str(), nullptr, 10));
			} else {
				r_value = Variant(strtod(number.c_str(), nullptr));
			}
			return true;
		}
		return fail("Valeur inattendue");
	}
};

} // namespace

String JSON::stringify(const Variant &p_value, const String &p_indent) {
	String out;
	_write(p_value, p_indent, 0, out);
	return out;
}

bool JSON::parse(const String &p_text, Variant &r_value, String *r_error) {
	Parser parser(p_text);
	// Marque d'ordre des octets UTF-8 éventuelle.
	if (p_text.size() >= 3 && (unsigned char)p_text[0] == 0xEF && (unsigned char)p_text[1] == 0xBB && (unsigned char)p_text[2] == 0xBF) {
		parser.pos = 3;
	}
	Variant value;
	bool ok = parser.parse_value(value, 0);
	if (ok) {
		parser.skip_space();
		if (parser.pos != p_text.size()) {
			ok = parser.fail("Texte en trop après la valeur");
		}
	}
	if (!ok) {
		if (r_error) {
			*r_error = parser.error;
		}
		return false;
	}
	r_value = value;
	return true;
}

String JSONSingleton::stringify(const Variant &p_value, const String &p_indent) {
	return JSON::stringify(p_value, p_indent);
}

Variant JSONSingleton::parse_string(const String &p_text) {
	Variant value;
	String error;
	if (!JSON::parse(p_text, value, &error)) {
		print_error("JSON.parse_string : " + error);
		return Variant();
	}
	return value;
}

void JSONSingleton::_bind_methods() {
	ClassDB::bind_method(D_METHOD("stringify", "value", "indent"), &JSONSingleton::stringify, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("parse_string", "text"), &JSONSingleton::parse_string);
}
