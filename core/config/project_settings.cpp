#include "project_settings.h"

#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/string/ustring.h"

#include <filesystem>

ProjectSettings *ProjectSettings::singleton = nullptr;
const char *ProjectSettings::PROJECT_FILE = "project.mex";

void ProjectSettings::_register_defaults() {
	defaults["application/name"] = Variant("Nouveau projet");
	defaults["application/main_scene"] = Variant("");
	defaults["application/icon"] = Variant(""); // image de l'icône de la fenêtre ; vide : icône de MEX
	defaults["display/width"] = Variant(1152);
	defaults["display/height"] = Variant(648);
	defaults["display/resizable"] = Variant(true);
	defaults["display/fullscreen"] = Variant(false);
	defaults["display/vsync"] = Variant(true);
	defaults["physics/gravity"] = Variant(980.0); // 2D, en pixels/s²
	defaults["physics/3d/gravity"] = Variant(9.8); // 3D, en m/s² (vers -Y)
	defaults["physics/ticks_per_second"] = Variant(60);
	defaults["rendering/clear_color"] = Variant(Color(0.3f, 0.3f, 0.3f, 1.0f));
	defaults["rendering/texture_filter"] = Variant("linear"); // ou "nearest" pour le pixel art
	defaults["rendering/3d/msaa"] = Variant(4); // 0, 2, 4 ou 8 échantillons
	defaults["rendering/3d/shadow_size"] = Variant(2048);
	defaults["rendering/3d/shadow_distance"] = Variant(40.0);
	defaults["debug/visible_collision_shapes"] = Variant(false);
	settings = defaults;
}

Error ProjectSettings::setup(const String &p_path) {
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::path abs = fs::absolute(fs::u8path(p_path.empty() ? "." : p_path), ec);
	if (ec) {
		return ERR_FILE_NOT_FOUND;
	}
	resource_path = str_replace(abs.lexically_normal().u8string(), "\\", "/");
	while (resource_path.size() > 1 && resource_path.back() == '/') {
		resource_path.pop_back();
	}
	settings = defaults;
	loaded = false;

	String file = String("res://") + PROJECT_FILE;
	if (!FileAccess::exists(file)) {
		return ERR_FILE_NOT_FOUND;
	}
	ConfigFile cfg;
	Error err = cfg.load(file);
	if (err != OK) {
		return err;
	}
	std::vector<String> sections;
	cfg.get_sections(&sections);
	for (const String &section : sections) {
		std::vector<String> keys;
		cfg.get_section_keys(section, &keys);
		for (const String &key : keys) {
			settings[section + "/" + key] = cfg.get_value(section, key);
		}
	}
	loaded = true;
	return OK;
}

Error ProjectSettings::save() {
	ConfigFile cfg;
	for (auto &kv : settings) {
		size_t slash = kv.first.find('/');
		if (slash == String::npos) {
			continue;
		}
		// N'écrit que ce qui diffère des valeurs par défaut.
		auto def = defaults.find(kv.first);
		if (def != defaults.end() && def->second == kv.second) {
			continue;
		}
		cfg.set_value(kv.first.substr(0, slash), kv.first.substr(slash + 1), kv.second);
	}
	if (!cfg.has_section_key("application", "name")) {
		cfg.set_value("application", "name", get_setting("application/name"));
	}
	Error err = cfg.save(String("res://") + PROJECT_FILE);
	if (err == OK) {
		loaded = true;
	}
	return err;
}

Variant ProjectSettings::get_setting(const String &p_name, const Variant &p_default) const {
	auto it = settings.find(p_name);
	return it == settings.end() ? p_default : it->second;
}

void ProjectSettings::set_setting(const String &p_name, const Variant &p_value) {
	settings[p_name] = p_value;
}

bool ProjectSettings::has_setting(const String &p_name) const {
	return settings.count(p_name) > 0;
}

void ProjectSettings::erase_setting(const String &p_name) {
	settings.erase(p_name);
}

String ProjectSettings::globalize_path(const String &p_path) const {
	if (str_begins_with(p_path, "res://")) {
		return path_join(resource_path, p_path.substr(6));
	}
	return p_path;
}

String ProjectSettings::localize_path(const String &p_path) const {
	if (str_begins_with(p_path, "res://")) {
		return path_simplify(p_path);
	}
	String path = str_replace(p_path, "\\", "/");
	if (!resource_path.empty() && str_begins_with(path, resource_path + "/")) {
		return "res://" + path.substr(resource_path.size() + 1);
	}
	return path;
}

void ProjectSettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_setting", "name", "default"), &ProjectSettings::get_setting, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("set_setting", "name", "value"), &ProjectSettings::set_setting);
	ClassDB::bind_method(D_METHOD("has_setting", "name"), &ProjectSettings::has_setting);
	ClassDB::bind_method(D_METHOD("globalize_path", "path"), &ProjectSettings::globalize_path);
	ClassDB::bind_method(D_METHOD("localize_path", "path"), &ProjectSettings::localize_path);
}

ProjectSettings::ProjectSettings() {
	singleton = this;
	_register_defaults();
}

ProjectSettings::~ProjectSettings() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
