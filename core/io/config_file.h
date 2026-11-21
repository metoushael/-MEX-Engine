// Fichier de configuration à sections : [section] puis cle = valeur.
#pragma once

#include "core/variant/variant.h"

class ConfigFile {
	typedef std::vector<std::pair<String, Variant>> Section;
	std::vector<std::pair<String, Section>> sections;

	Section *_find(const String &p_section);
	const Section *_find(const String &p_section) const;

public:
	Error load(const String &p_path);
	Error parse(const String &p_text, const String &p_source_name = "");
	Error save(const String &p_path) const;
	String to_text() const;

	void set_value(const String &p_section, const String &p_key, const Variant &p_value);
	Variant get_value(const String &p_section, const String &p_key, const Variant &p_default = Variant()) const;
	bool has_section_key(const String &p_section, const String &p_key) const;
	void get_sections(std::vector<String> *r_sections) const;
	void get_section_keys(const String &p_section, std::vector<String> *r_keys) const;
	void clear() { sections.clear(); }
};
