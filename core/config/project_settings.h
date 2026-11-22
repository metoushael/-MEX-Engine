// Paramètres du projet (fichier project.mex) et conversion des chemins res://.
#pragma once

#include "core/object/object.h"

class ProjectSettings : public Object {
	GDCLASS(ProjectSettings, Object)

	static ProjectSettings *singleton;

	String resource_path; // dossier absolu du projet
	bool loaded = false;
	std::map<String, Variant> settings; // "section/cle" -> valeur
	std::map<String, Variant> defaults;

	void _register_defaults();

protected:
	static void _bind_methods();

public:
	static const char *PROJECT_FILE;
	static ProjectSettings *get_singleton() { return singleton; }

	Error setup(const String &p_path); // charge <p_path>/project.mex
	Error save();
	bool is_loaded() const { return loaded; }

	Variant get_setting(const String &p_name, const Variant &p_default = Variant()) const;
	void set_setting(const String &p_name, const Variant &p_value);
	bool has_setting(const String &p_name) const;
	void erase_setting(const String &p_name);
	const std::map<String, Variant> &get_all_settings() const { return settings; }

	String get_resource_path() const { return resource_path; }
	String globalize_path(const String &p_path) const;
	String localize_path(const String &p_path) const;

	ProjectSettings();
	~ProjectSettings();
};

#define GLOBAL_GET(m_name) ProjectSettings::get_singleton()->get_setting(m_name)
