#include "ustring.h"

#include <cstdarg>
#include <cstring>
#include <mutex>

// ---------------------------------------------------------------------------
// Journalisation
// ---------------------------------------------------------------------------
static std::vector<std::pair<int, PrintHandler>> &_handlers() {
	static std::vector<std::pair<int, PrintHandler>> handlers;
	return handlers;
}
static int _handler_next_id = 1;

static void _dispatch(PrintLevel p_level, const String &p_text) {
	static bool in_dispatch = false;
	switch (p_level) {
		case PRINT_INFO:
			fprintf(stdout, "%s\n", p_text.c_str());
			fflush(stdout);
			break;
		case PRINT_WARNING:
			fprintf(stderr, "ATTENTION: %s\n", p_text.c_str());
			break;
		case PRINT_ERROR:
			fprintf(stderr, "ERREUR: %s\n", p_text.c_str());
			break;
	}
	if (in_dispatch) {
		return; // évite la récursion si un handler journalise lui-même
	}
	in_dispatch = true;
	for (auto &h : _handlers()) {
		h.second(p_level, p_text);
	}
	in_dispatch = false;
}

void print_line(const String &p_text) { _dispatch(PRINT_INFO, p_text); }
void print_warning(const String &p_text) { _dispatch(PRINT_WARNING, p_text); }
void print_error(const String &p_text) { _dispatch(PRINT_ERROR, p_text); }

int add_print_handler(PrintHandler p_handler) {
	int id = _handler_next_id++;
	_handlers().push_back({ id, p_handler });
	return id;
}

void remove_print_handler(int p_id) {
	auto &h = _handlers();
	for (size_t i = 0; i < h.size(); i++) {
		if (h[i].first == p_id) {
			h.erase(h.begin() + i);
			return;
		}
	}
}

String vformat(const char *p_fmt, ...) {
	va_list args;
	va_start(args, p_fmt);
	va_list copy;
	va_copy(copy, args);
	int len = vsnprintf(nullptr, 0, p_fmt, copy);
	va_end(copy);
	String out;
	if (len > 0) {
		out.resize(len);
		vsnprintf(&out[0], len + 1, p_fmt, args);
	}
	va_end(args);
	return out;
}

// ---------------------------------------------------------------------------
// Chaînes
// ---------------------------------------------------------------------------
String itos(int64_t p_val) {
	return std::to_string(p_val);
}

String rtos(double p_val) {
	if (std::isnan(p_val)) {
		return "nan";
	}
	if (std::isinf(p_val)) {
		return p_val > 0 ? "inf" : "-inf";
	}
	char buf[64];
	// Cherche la représentation la plus courte qui redonne la même valeur.
	for (int prec = 6; prec <= 17; prec++) {
		snprintf(buf, sizeof(buf), "%.*g", prec, p_val);
		if (strtod(buf, nullptr) == p_val || (float)strtod(buf, nullptr) == (float)p_val) {
			break;
		}
	}
	return buf;
}

bool str_begins_with(const String &p_s, const String &p_prefix) {
	return p_s.size() >= p_prefix.size() && p_s.compare(0, p_prefix.size(), p_prefix) == 0;
}

bool str_ends_with(const String &p_s, const String &p_suffix) {
	return p_s.size() >= p_suffix.size() && p_s.compare(p_s.size() - p_suffix.size(), p_suffix.size(), p_suffix) == 0;
}

std::vector<String> str_split(const String &p_s, const String &p_delim, bool p_allow_empty) {
	std::vector<String> out;
	if (p_delim.empty()) {
		out.push_back(p_s);
		return out;
	}
	size_t from = 0;
	while (true) {
		size_t pos = p_s.find(p_delim, from);
		String part = p_s.substr(from, pos == String::npos ? String::npos : pos - from);
		if (p_allow_empty || !part.empty()) {
			out.push_back(part);
		}
		if (pos == String::npos) {
			break;
		}
		from = pos + p_delim.size();
	}
	return out;
}

String str_join(const std::vector<String> &p_parts, const String &p_sep) {
	String out;
	for (size_t i = 0; i < p_parts.size(); i++) {
		if (i) {
			out += p_sep;
		}
		out += p_parts[i];
	}
	return out;
}

String str_strip(const String &p_s) {
	size_t b = 0, e = p_s.size();
	while (b < e && (unsigned char)p_s[b] <= ' ') {
		b++;
	}
	while (e > b && (unsigned char)p_s[e - 1] <= ' ') {
		e--;
	}
	return p_s.substr(b, e - b);
}

String str_replace(const String &p_s, const String &p_from, const String &p_to) {
	if (p_from.empty()) {
		return p_s;
	}
	String out;
	size_t from = 0;
	while (true) {
		size_t pos = p_s.find(p_from, from);
		if (pos == String::npos) {
			out += p_s.substr(from);
			break;
		}
		out += p_s.substr(from, pos - from);
		out += p_to;
		from = pos + p_from.size();
	}
	return out;
}

String str_to_lower(const String &p_s) {
	String out = p_s;
	for (char &c : out) {
		if (c >= 'A' && c <= 'Z') {
			c = c - 'A' + 'a';
		}
	}
	return out;
}

String str_to_upper(const String &p_s) {
	String out = p_s;
	for (char &c : out) {
		if (c >= 'a' && c <= 'z') {
			c = c - 'a' + 'A';
		}
	}
	return out;
}

String str_capitalize(const String &p_s) {
	String out;
	bool upper_next = true;
	for (char c : p_s) {
		if (c == '_') {
			out += ' ';
			upper_next = true;
		} else if (upper_next) {
			out += (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
			upper_next = false;
		} else {
			out += c;
		}
	}
	return out;
}

String str_c_escape(const String &p_s) {
	String out;
	for (char c : p_s) {
		switch (c) {
			case '\\': out += "\\\\"; break;
			case '"': out += "\\\""; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			case '\r': out += "\\r"; break;
			default: out += c;
		}
	}
	return out;
}

bool str_is_valid_identifier(const String &p_s) {
	if (p_s.empty()) {
		return false;
	}
	for (size_t i = 0; i < p_s.size(); i++) {
		char c = p_s[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (i > 0 && c >= '0' && c <= '9');
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool str_match_nocase(const String &p_s, const String &p_filter) {
	if (p_filter.empty()) {
		return true;
	}
	return str_to_lower(p_s).find(str_to_lower(p_filter)) != String::npos;
}

// ---------------------------------------------------------------------------
// Chemins
// ---------------------------------------------------------------------------
String path_get_file(const String &p_path) {
	size_t pos = p_path.find_last_of("/\\");
	return pos == String::npos ? p_path : p_path.substr(pos + 1);
}

String path_get_extension(const String &p_path) {
	String file = path_get_file(p_path);
	size_t pos = file.rfind('.');
	if (pos == String::npos || pos == 0) {
		return "";
	}
	return str_to_lower(file.substr(pos + 1));
}

String path_get_base_dir(const String &p_path) {
	// "res://a/b.png" -> "res://a" ; "res://b.png" -> "res://"
	size_t proto = p_path.find("://");
	size_t pos = p_path.find_last_of("/\\");
	if (pos == String::npos) {
		return "";
	}
	if (proto != String::npos && pos <= proto + 2) {
		return p_path.substr(0, proto + 3);
	}
	return p_path.substr(0, pos);
}

String path_get_basename(const String &p_path) {
	size_t slash = p_path.find_last_of("/\\");
	size_t dot = p_path.rfind('.');
	if (dot == String::npos || (slash != String::npos && dot < slash)) {
		return p_path;
	}
	return p_path.substr(0, dot);
}

String path_join(const String &p_a, const String &p_b) {
	if (p_a.empty()) {
		return p_b;
	}
	if (p_b.empty()) {
		return p_a;
	}
	bool a_slash = p_a.back() == '/' || p_a.back() == '\\';
	bool b_slash = p_b.front() == '/' || p_b.front() == '\\';
	if (a_slash && b_slash) {
		return p_a + p_b.substr(1);
	}
	if (a_slash || b_slash) {
		return p_a + p_b;
	}
	return p_a + "/" + p_b;
}

String path_simplify(const String &p_path) {
	String s = str_replace(p_path, "\\", "/");
	String prefix;
	size_t proto = s.find("://");
	if (proto != String::npos) {
		prefix = s.substr(0, proto + 3);
		s = s.substr(proto + 3);
	} else if (!s.empty() && s[0] == '/') {
		prefix = "/";
		s = s.substr(1);
	}
	std::vector<String> parts = str_split(s, "/", false);
	std::vector<String> out;
	for (const String &p : parts) {
		if (p == ".") {
			continue;
		}
		if (p == ".." && !out.empty() && out.back() != "..") {
			out.pop_back();
			continue;
		}
		out.push_back(p);
	}
	return prefix + str_join(out, "/");
}

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------
std::vector<uint32_t> utf8_decode(const String &p_s) {
	std::vector<uint32_t> out;
	out.reserve(p_s.size());
	size_t i = 0;
	const size_t n = p_s.size();
	while (i < n) {
		unsigned char c = (unsigned char)p_s[i];
		uint32_t cp = 0;
		int extra = 0;
		if (c < 0x80) {
			cp = c;
		} else if ((c & 0xE0) == 0xC0) {
			cp = c & 0x1F;
			extra = 1;
		} else if ((c & 0xF0) == 0xE0) {
			cp = c & 0x0F;
			extra = 2;
		} else if ((c & 0xF8) == 0xF0) {
			cp = c & 0x07;
			extra = 3;
		} else {
			cp = 0xFFFD;
		}
		i++;
		for (int k = 0; k < extra; k++) {
			if (i < n && ((unsigned char)p_s[i] & 0xC0) == 0x80) {
				cp = (cp << 6) | ((unsigned char)p_s[i] & 0x3F);
				i++;
			} else {
				cp = 0xFFFD;
				break;
			}
		}
		out.push_back(cp);
	}
	return out;
}

String utf8_encode(uint32_t p_cp) {
	String out;
	if (p_cp < 0x80) {
		out += (char)p_cp;
	} else if (p_cp < 0x800) {
		out += (char)(0xC0 | (p_cp >> 6));
		out += (char)(0x80 | (p_cp & 0x3F));
	} else if (p_cp < 0x10000) {
		out += (char)(0xE0 | (p_cp >> 12));
		out += (char)(0x80 | ((p_cp >> 6) & 0x3F));
		out += (char)(0x80 | (p_cp & 0x3F));
	} else {
		out += (char)(0xF0 | (p_cp >> 18));
		out += (char)(0x80 | ((p_cp >> 12) & 0x3F));
		out += (char)(0x80 | ((p_cp >> 6) & 0x3F));
		out += (char)(0x80 | (p_cp & 0x3F));
	}
	return out;
}
