#include "editor_export.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/pack.h"
#include "core/os/os.h"
#include "core/string/ustring.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

const char *EditorExport::PRESETS_FILE = "export_presets.cfg";

namespace {

const char *PLATFORM_WINDOWS = "Windows Desktop";
const char *PLATFORM_LINUX = "Linux";
const char *PLATFORM_MACOS = "macOS";

String _generic(const fs::path &p_path) {
	return str_replace(p_path.lexically_normal().u8string(), "\\", "/");
}

// Nom de fichier sûr à partir du nom du jeu.
String _file_name(const String &p_name) {
	String out;
	for (unsigned char c : p_name) {
		if (std::isalnum(c) || c == '-' || c == '_' || c >= 0x80) {
			out += (char)c;
		} else if (c == ' ' && !out.empty() && out.back() != '_') {
			out += '_';
		}
	}
	while (!out.empty() && out.back() == '_') {
		out.pop_back();
	}
	return out.empty() ? String("jeu") : out;
}

// Dossier des modèles et nom de l'exécutable pour un système.
String _platform_id(const String &p_platform) {
	if (p_platform == PLATFORM_WINDOWS) {
		return "windows-x86_64";
	}
	return p_platform == PLATFORM_MACOS ? "macos-universal" : "linux-x86_64";
}

String _template_name(const String &p_platform) {
	return p_platform == PLATFORM_WINDOWS ? "mex-jeu.exe" : "mex-jeu";
}

// Comparaison avec jokers, portée de String::match de Godot (core/string/ustring.cpp).
bool _wildcard(const char *p_pattern, const char *p_text) {
	switch (*p_pattern) {
		case '\0':
			return !*p_text;
		case '*':
			return _wildcard(p_pattern + 1, p_text) || (*p_text && _wildcard(p_pattern, p_text + 1));
		case '?':
			return *p_text && *p_text != '.' && _wildcard(p_pattern + 1, p_text + 1);
		default:
			return std::tolower((unsigned char)*p_pattern) == std::tolower((unsigned char)*p_text) && _wildcard(p_pattern + 1, p_text + 1);
	}
}

void _make_executable(const fs::path &p_path) {
	std::error_code ec;
	fs::permissions(p_path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
}

String _xml(const String &p_text) {
	String out;
	for (char c : p_text) {
		switch (c) {
			case '&':
				out += "&amp;";
				break;
			case '<':
				out += "&lt;";
				break;
			case '>':
				out += "&gt;";
				break;
			default:
				out += c;
		}
	}
	return out;
}

} // namespace

std::vector<String> EditorExport::get_platforms() {
	return { PLATFORM_WINDOWS, PLATFORM_LINUX, PLATFORM_MACOS };
}

String EditorExport::get_host_platform() {
#if defined(_WIN32)
	return PLATFORM_WINDOWS;
#elif defined(__APPLE__)
	return PLATFORM_MACOS;
#else
	return PLATFORM_LINUX;
#endif
}

ExportPreset EditorExport::make_preset(const String &p_platform, const String &p_game_name) {
	ExportPreset preset;
	preset.name = p_platform;
	preset.platform = p_platform;
	const String name = _file_name(p_game_name);
	// Un sous-dossier par système, et les extensions habituelles de Godot.
	if (p_platform == PLATFORM_WINDOWS) {
		preset.export_path = "export/windows/" + name + ".exe";
	} else if (p_platform == PLATFORM_MACOS) {
		preset.export_path = "export/macos/" + name + ".app";
	} else {
		preset.export_path = "export/linux/" + name + ".x86_64";
	}
	String id;
	for (unsigned char c : name) {
		id += std::isalnum(c) ? (char)std::tolower(c) : '-';
	}
	preset.bundle_identifier = "com.exemple." + id;
	return preset;
}

std::vector<ExportPreset> EditorExport::load_presets(const String &p_project_dir) {
	std::vector<ExportPreset> presets;
	const String path = path_join(p_project_dir, PRESETS_FILE);
	ConfigFile config;
	if (!FileAccess::exists(path) || config.load(path) != OK) {
		return presets;
	}
	for (int i = 0;; i++) {
		const String section = "preset." + itos(i);
		if (!config.has_section_key(section, "name")) {
			break;
		}
		ExportPreset preset;
		preset.name = config.get_value(section, "name", Variant("")).as_string();
		preset.platform = config.get_value(section, "platform", Variant(PLATFORM_LINUX)).as_string();
		if (preset.platform == "Linux/X11") {
			preset.platform = PLATFORM_LINUX; // nom de Godot 3
		}
		preset.export_path = config.get_value(section, "export_path", Variant("")).as_string();
		preset.include_filter = config.get_value(section, "include_filter", Variant("")).as_string();
		preset.exclude_filter = config.get_value(section, "exclude_filter", Variant("")).as_string();
		const String options = section + ".options";
		preset.custom_template = config.get_value(options, "custom_template/release", Variant("")).as_string();
		preset.embed_pck = config.get_value(options, "binary_format/embed_pck", Variant(false)).as_bool();
		preset.bundle_identifier = config.get_value(options, "application/bundle_identifier", Variant("")).as_string();
		presets.push_back(preset);
	}
	return presets;
}

Error EditorExport::save_presets(const String &p_project_dir, const std::vector<ExportPreset> &p_presets) {
	ConfigFile config;
	for (size_t i = 0; i < p_presets.size(); i++) {
		const ExportPreset &preset = p_presets[i];
		const String section = "preset." + itos((int64_t)i);
		config.set_value(section, "name", Variant(preset.name));
		config.set_value(section, "platform", Variant(preset.platform));
		config.set_value(section, "runnable", Variant(preset.platform == get_host_platform()));
		config.set_value(section, "export_filter", Variant("all_resources"));
		config.set_value(section, "include_filter", Variant(preset.include_filter));
		config.set_value(section, "exclude_filter", Variant(preset.exclude_filter));
		config.set_value(section, "export_path", Variant(preset.export_path));
		const String options = section + ".options";
		config.set_value(options, "custom_template/release", Variant(preset.custom_template));
		config.set_value(options, "binary_format/embed_pck", Variant(preset.embed_pck));
		if (preset.platform == PLATFORM_MACOS) {
			config.set_value(options, "application/bundle_identifier", Variant(preset.bundle_identifier));
		}
	}
	return config.save(path_join(p_project_dir, PRESETS_FILE));
}

String EditorExport::get_template_hint(const String &p_platform) {
	const String editor = OS::get_singleton()->get_executable_path();
	return path_join(path_join(path_join(path_get_base_dir(editor), "templates"), _platform_id(p_platform)), _template_name(p_platform));
}

String EditorExport::find_template(const String &p_platform) {
	std::error_code ec;
	std::vector<String> candidates;
	candidates.push_back(get_template_hint(p_platform));
	if (char *pref = SDL_GetPrefPath("Metoushela", "MEX")) {
		candidates.push_back(path_join(path_join(String(pref) + "export_templates", _platform_id(p_platform)), _template_name(p_platform)));
		SDL_free(pref);
	}
	const String editor = OS::get_singleton()->get_executable_path();
	if (p_platform == get_host_platform() && !editor.empty()) {
		// Même système : « mex-jeu » livré à côté de l'éditeur, sinon l'éditeur lui-même.
		candidates.push_back(path_join(path_get_base_dir(editor), _template_name(p_platform)));
		candidates.push_back(editor);
	}
	for (const String &candidate : candidates) {
		if (fs::is_regular_file(fs::u8path(candidate), ec)) {
			return candidate;
		}
	}
	return String();
}

bool EditorExport::match_filter(const String &p_path, const String &p_filter_list) {
	const String file = path_get_file(p_path);
	for (const String &part : str_split(p_filter_list, ",", false)) {
		const String filter = str_strip(part);
		if (filter.empty()) {
			continue;
		}
		if (_wildcard(filter.c_str(), p_path.c_str()) || _wildcard(filter.c_str(), file.c_str())) {
			return true;
		}
	}
	return false;
}

bool EditorExport::export_preset(const String &p_project_dir, const ExportPreset &p_preset, String &r_message) {
	std::error_code ec;
	const fs::path project_dir = fs::u8path(p_project_dir);
	if (str_strip(p_preset.export_path).empty()) {
		r_message = "Indiquez le chemin du fichier à produire.";
		return false;
	}
	fs::path target = fs::u8path(str_strip(p_preset.export_path));
	if (target.is_relative()) {
		target = project_dir / target;
	}
	target = target.lexically_normal();
	const bool macos = p_preset.platform == PLATFORM_MACOS;
	const bool windows = p_preset.platform == PLATFORM_WINDOWS;
	if (windows && target.extension() != ".exe") {
		target += ".exe";
	}
	if (macos && target.extension() != ".app") {
		target += ".app";
	}
	const fs::path out_dir = target.parent_path();
	if (out_dir.empty() || fs::equivalent(out_dir, project_dir, ec)) {
		r_message = "Choisissez un autre dossier que celui du projet (un sous-dossier convient).";
		return false;
	}

	const String template_path = p_preset.custom_template.empty() ? find_template(p_preset.platform) : p_preset.custom_template;
	if (template_path.empty() || !fs::is_regular_file(fs::u8path(template_path), ec)) {
		r_message = "Modèle d'export introuvable pour " + p_preset.platform + ". Placez l'exécutable « " + _template_name(p_preset.platform) + " » de ce système ici :\n  " + get_template_hint(p_preset.platform) + "\nou indiquez son chemin dans « Modèle personnalisé ».";
		return false;
	}
	ec.clear();
	fs::create_directories(out_dir, ec);
	if (ec) {
		r_message = "Impossible de créer le dossier : " + _generic(out_dir);
		return false;
	}

	// Fichiers du projet. Écartés d'office : fichiers et dossiers cachés, archives .pck,
	// export_presets.cfg, le dossier d'export « export/ » et celui du préréglage, et les
	// dossiers qui contiennent un fichier « .mexignore » (l'équivalent du .gdignore de Godot).
	// Le filtre d'inclusion repêche un fichier écarté ; le filtre d'exclusion a le dernier mot.
	std::vector<std::pair<String, String>> files;
	uint64_t total_size = 0;
	const fs::path default_export_dir = project_dir / "export";
	for (fs::recursive_directory_iterator it(project_dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
		const fs::path &path = it->path();
		const String name = path.filename().u8string();
		std::error_code ec2;
		if (it->is_directory(ec2)) {
			if (fs::equivalent(path, out_dir, ec2) || fs::equivalent(path, default_export_dir, ec2) || fs::exists(path / ".mexignore", ec2)) {
				it.disable_recursion_pending();
			}
			continue;
		}
		if (!it->is_regular_file(ec2)) {
			continue;
		}
		const String relative = _generic(fs::relative(path, project_dir, ec2));
		const String ext = path_get_extension(name);
		bool keep = !(relative[0] == '.' || relative.find("/.") != String::npos || ext == "pck" || name == PRESETS_FILE);
		if (!keep && name != PRESETS_FILE && name != ".mexignore" && match_filter(relative, p_preset.include_filter)) {
			keep = true;
		}
		if (keep && match_filter(relative, p_preset.exclude_filter) && name != ProjectSettings::PROJECT_FILE) {
			keep = false;
		}
		if (keep) {
			files.push_back({ relative, _generic(path) });
			total_size += (uint64_t)it->file_size(ec2);
		}
	}
	std::sort(files.begin(), files.end());

	String exe_name = target.stem().u8string();
	fs::path exe_path = target;
	fs::path pack_dir = out_dir;
	if (macos) {
		// Paquet d'application : Jeu.app/Contents/{Info.plist, MacOS/<exécutable>, Resources/<archive>}.
		exe_name = _file_name(exe_name);
		ec.clear();
		fs::remove_all(target, ec);
		fs::create_directories(target / "Contents" / "MacOS", ec);
		fs::create_directories(target / "Contents" / "Resources", ec);
		if (ec) {
			r_message = "Impossible de créer le paquet : " + _generic(target);
			return false;
		}
		exe_path = target / "Contents" / "MacOS" / fs::u8path(exe_name);
		pack_dir = target / "Contents" / "Resources";
	}
	if (FileAccess::copy(template_path, _generic(exe_path)) != OK) {
		r_message = "Impossible de copier l'exécutable vers : " + _generic(exe_path);
		return false;
	}
	_make_executable(exe_path);

	const bool embed = p_preset.embed_pck && !macos; // macOS : une signature ne survit pas à un ajout
	const fs::path pack_path = embed ? exe_path : pack_dir / fs::u8path(exe_name + ".pck");
	if (PackedData::create(_generic(pack_path), files, embed) != OK) {
		r_message = "Échec de l'écriture de l'archive : " + _generic(pack_path);
		return false;
	}

	int libraries = 0;
	if (!macos) {
		// Bibliothèques placées à côté du modèle (SDL2.dll quand SDL n'est pas liée statiquement).
		for (fs::directory_iterator it(fs::u8path(template_path).parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
			if (path_get_extension(it->path().filename().u8string()) == "dll" && windows) {
				if (FileAccess::copy(_generic(it->path()), _generic(out_dir / it->path().filename())) == OK) {
					libraries++;
				}
			}
		}
	} else {
		const String game_name = GLOBAL_GET("application/name").as_string();
		const String identifier = p_preset.bundle_identifier.empty() ? String("com.exemple.jeu") : p_preset.bundle_identifier;
		// Icône : icon.icns à la racine du projet, sinon celle de MEX livrée avec l'éditeur.
		bool has_icon = false;
		const String editor_dir = path_get_base_dir(OS::get_singleton()->get_executable_path());
		for (const String &icon : { path_join(p_project_dir, "icon.icns"), path_join(editor_dir, "branding/mex.icns"), path_join(editor_dir, "mex.icns") }) {
			if (FileAccess::exists(icon) && FileAccess::copy(icon, _generic(pack_dir / "icon.icns")) == OK) {
				has_icon = true;
				break;
			}
		}
		std::ofstream plist(target / "Contents" / "Info.plist", std::ios::binary | std::ios::trunc);
		plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			  << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
			  << "<plist version=\"1.0\">\n<dict>\n"
			  << "\t<key>CFBundleExecutable</key>\n\t<string>" << _xml(exe_name) << "</string>\n"
			  << "\t<key>CFBundleIdentifier</key>\n\t<string>" << _xml(identifier) << "</string>\n"
			  << "\t<key>CFBundleName</key>\n\t<string>" << _xml(game_name) << "</string>\n"
			  << "\t<key>CFBundleDisplayName</key>\n\t<string>" << _xml(game_name) << "</string>\n"
			  << "\t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n"
			  << "\t<key>CFBundleInfoDictionaryVersion</key>\n\t<string>6.0</string>\n"
			  << "\t<key>CFBundleShortVersionString</key>\n\t<string>1.0</string>\n"
			  << "\t<key>CFBundleVersion</key>\n\t<string>1.0</string>\n";
		if (has_icon) {
			plist << "\t<key>CFBundleIconFile</key>\n\t<string>icon.icns</string>\n";
		}
		plist << "\t<key>LSMinimumSystemVersion</key>\n\t<string>10.13</string>\n"
			  << "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n"
			  << "</dict>\n</plist>\n";
		if (!plist.good()) {
			r_message = "Impossible d'écrire Info.plist dans : " + _generic(target);
			return false;
		}
	}

	const String size_text = itos((int64_t)files.size()) + " fichiers, " + itos((int64_t)((total_size + 1023) / 1024)) + " Ko";
	r_message = "« " + p_preset.name + " » exporté : " + _generic(target);
	if (macos) {
		r_message += "\n  paquet d'application, archive dans Contents/Resources (" + size_text + ")";
	} else if (embed) {
		r_message += "\n  un seul fichier, archive incorporée (" + size_text + ")";
	} else {
		r_message += "\n  " + exe_name + ".pck à côté de l'exécutable (" + size_text + ")";
	}
	if (libraries > 0) {
		r_message += "\n  " + itos(libraries) + " bibliothèque(s)";
	}
	return true;
}
