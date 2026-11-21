#include "config_file.h"

#include "core/io/file_access.h"
#include "core/io/variant_parser.h"

ConfigFile::Section *ConfigFile::_find(const String &p_section) {
	for (auto &s : sections) {
		if (s.first == p_section) {
			return &s.second;
		}
	}
	return nullptr;
}

const ConfigFile::Section *ConfigFile::_find(const String &p_section) const {
	for (auto &s : sections) {
		if (s.first == p_section) {
			return &s.second;
		}
	}
	return nullptr;
}

Error ConfigFile::load(const String &p_path) {
	Error err;
	String text = FileAccess::get_file_as_string(p_path, &err);
	if (err != OK) {
		return err;
	}
	return parse(text, p_path);
}

Error ConfigFile::parse(const String &p_text, const String &p_source_name) {
	sections.clear();
	VariantParser::Stream stream(p_text);
	String current;
	while (true) {
		VariantParser::Tag tag;
		String key, err_text;
		Variant value;
		VariantParser::Item item = VariantParser::parse_next(stream, tag, key, value, err_text);
		if (item == VariantParser::ITEM_EOF) {
			break;
		}
		if (item == VariantParser::ITEM_ERROR) {
			print_error(p_source_name + ":" + std::to_string(stream.line) + " - " + err_text);
			return ERR_PARSE_ERROR;
		}
		if (item == VariantParser::ITEM_TAG) {
			current = tag.name;
			if (!_find(current)) {
				sections.push_back({ current, Section() });
			}
		} else {
			set_value(current, key, value);
		}
	}
	return OK;
}

String ConfigFile::to_text() const {
	String out;
	for (auto &s : sections) {
		if (s.second.empty()) {
			continue;
		}
		if (!s.first.empty()) {
			out += "[" + s.first + "]\n";
		}
		for (auto &kv : s.second) {
			out += kv.first + " = " + VariantWriter::write(kv.second) + "\n";
		}
		out += "\n";
	}
	return out;
}

Error ConfigFile::save(const String &p_path) const {
	return FileAccess::write_string(p_path, to_text());
}

void ConfigFile::set_value(const String &p_section, const String &p_key, const Variant &p_value) {
	Section *s = _find(p_section);
	if (!s) {
		sections.push_back({ p_section, Section() });
		s = &sections.back().second;
	}
	for (auto &kv : *s) {
		if (kv.first == p_key) {
			kv.second = p_value;
			return;
		}
	}
	s->push_back({ p_key, p_value });
}

Variant ConfigFile::get_value(const String &p_section, const String &p_key, const Variant &p_default) const {
	const Section *s = _find(p_section);
	if (s) {
		for (auto &kv : *s) {
			if (kv.first == p_key) {
				return kv.second;
			}
		}
	}
	return p_default;
}

bool ConfigFile::has_section_key(const String &p_section, const String &p_key) const {
	const Section *s = _find(p_section);
	if (s) {
		for (auto &kv : *s) {
			if (kv.first == p_key) {
				return true;
			}
		}
	}
	return false;
}

void ConfigFile::get_sections(std::vector<String> *r_sections) const {
	for (auto &s : sections) {
		r_sections->push_back(s.first);
	}
}

void ConfigFile::get_section_keys(const String &p_section, std::vector<String> *r_keys) const {
	const Section *s = _find(p_section);
	if (s) {
		for (auto &kv : *s) {
			r_keys->push_back(kv.first);
		}
	}
}
