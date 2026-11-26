// Paquet de modèles d'exportation (« .tpz ») : une archive ZIP qui réunit le « mex-jeu » de
// chaque système. Installée, elle permet d'exporter pour tous les systèmes depuis un éditeur
// qui n'a pas ses modèles incorporés (éditeur compilé soi-même, par exemple).
//
//   templates/version.txt                    version de MEX qui a produit les modèles
//   templates/windows-x86_64/mex-jeu.exe
//   templates/linux-x86_64/mex-jeu
//   templates/macos-universal/mex-jeu
//   templates/web-wasm32/mex-jeu.js
#include "editor_export.h"

#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "thirdparty/stb/stb_image.h"

#include <SDL.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

uint16_t u16(const uint8_t *p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t u32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t crc32(const uint8_t *p_data, size_t p_size) {
	static uint32_t table[256];
	static bool ready = false;
	if (!ready) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) {
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			}
			table[i] = c;
		}
		ready = true;
	}
	uint32_t crc = 0xffffffffu;
	for (size_t i = 0; i < p_size; i++) {
		crc = table[(crc ^ p_data[i]) & 0xff] ^ (crc >> 8);
	}
	return crc ^ 0xffffffffu;
}

struct ZipEntry {
	String name;
	uint32_t method = 0, crc = 0, compressed = 0, size = 0, offset = 0;
};

// Lit le répertoire central d'une archive ZIP (sans ZIP64 : les modèles font quelques Mo).
bool read_zip_directory(const std::vector<uint8_t> &p_data, std::vector<ZipEntry> &r_entries, String &r_error) {
	if (p_data.size() < 22) {
		r_error = "le fichier est trop court pour être une archive ZIP";
		return false;
	}
	size_t end = String::npos;
	const size_t lowest = p_data.size() > 22 + 65535 ? p_data.size() - 22 - 65535 : 0;
	for (size_t i = p_data.size() - 22 + 1; i-- > lowest;) {
		if (u32(&p_data[i]) == 0x06054b50u) {
			end = i;
			break;
		}
	}
	if (end == String::npos) {
		r_error = "ce n'est pas une archive ZIP (fin du répertoire introuvable)";
		return false;
	}
	const size_t count = u16(&p_data[end + 10]);
	size_t at = u32(&p_data[end + 16]);
	for (size_t i = 0; i < count; i++) {
		if (at + 46 > p_data.size() || u32(&p_data[at]) != 0x02014b50u) {
			r_error = "répertoire de l'archive endommagé";
			return false;
		}
		ZipEntry entry;
		entry.method = u16(&p_data[at + 10]);
		entry.crc = u32(&p_data[at + 16]);
		entry.compressed = u32(&p_data[at + 20]);
		entry.size = u32(&p_data[at + 24]);
		const size_t name_length = u16(&p_data[at + 28]), extra = u16(&p_data[at + 30]), comment = u16(&p_data[at + 32]);
		entry.offset = u32(&p_data[at + 42]);
		if (at + 46 + name_length > p_data.size()) {
			r_error = "répertoire de l'archive endommagé";
			return false;
		}
		entry.name = String((const char *)&p_data[at + 46], name_length);
		r_entries.push_back(entry);
		at += 46 + name_length + extra + comment;
	}
	return true;
}

bool extract_zip_entry(const std::vector<uint8_t> &p_data, const ZipEntry &p_entry, std::vector<uint8_t> &r_out, String &r_error) {
	const size_t at = p_entry.offset;
	if (at + 30 > p_data.size() || u32(&p_data[at]) != 0x04034b50u) {
		r_error = "en-tête de « " + p_entry.name + " » endommagé";
		return false;
	}
	const size_t start = at + 30 + u16(&p_data[at + 26]) + u16(&p_data[at + 28]);
	if (start + p_entry.compressed > p_data.size()) {
		r_error = "« " + p_entry.name + " » dépasse la fin de l'archive";
		return false;
	}
	if (p_entry.method == 0) {
		r_out.assign(p_data.begin() + (long)start, p_data.begin() + (long)(start + p_entry.compressed));
	} else if (p_entry.method == 8) {
		int length = 0;
		char *raw = stbi_zlib_decode_noheader_malloc((const char *)&p_data[start], (int)p_entry.compressed, &length);
		if (!raw) {
			r_error = "« " + p_entry.name + " » ne se décompresse pas";
			return false;
		}
		r_out.assign((const uint8_t *)raw, (const uint8_t *)raw + length);
		free(raw);
	} else {
		r_error = "« " + p_entry.name + " » utilise une compression non prise en charge";
		return false;
	}
	if (r_out.size() != p_entry.size || crc32(r_out.data(), r_out.size()) != p_entry.crc) {
		r_error = "« " + p_entry.name + " » est endommagé (somme de contrôle)";
		return false;
	}
	return true;
}

const char *const TEMPLATE_IDS[] = { "windows-x86_64", "linux-x86_64", "macos-universal", "web-wasm32" };

String template_file_name(const String &p_id) {
	return p_id == "windows-x86_64" ? "mex-jeu.exe" : (p_id == "web-wasm32" ? "mex-jeu.js" : "mex-jeu");
}

} // namespace

String EditorExport::get_installed_templates_dir() {
	String dir;
	if (char *pref = SDL_GetPrefPath("Metoushela", "MEX")) {
		dir = String(pref) + "export_templates";
		SDL_free(pref);
	}
	return dir;
}

String EditorExport::get_installed_templates_version() {
	const String dir = get_installed_templates_dir();
	if (dir.empty()) {
		return String();
	}
	const String path = dir + "/version.txt";
	return FileAccess::exists(path) ? str_strip(FileAccess::get_file_as_string(path)) : String();
}

bool EditorExport::install_template_package(const String &p_package, String &r_message) {
	Error error = OK;
	const std::vector<uint8_t> data = FileAccess::get_file_as_bytes(p_package, &error);
	if (error != OK || data.empty()) {
		r_message = "Impossible de lire " + p_package;
		return false;
	}
	std::vector<ZipEntry> entries;
	String reason;
	if (!read_zip_directory(data, entries, reason)) {
		r_message = "Paquet de modèles illisible : " + reason + ".";
		return false;
	}
	const String dir = get_installed_templates_dir();
	if (dir.empty()) {
		r_message = "Dossier de préférences introuvable.";
		return false;
	}
	// D'abord tout extraire en mémoire : un paquet endommagé n'installe rien.
	String version;
	std::vector<std::pair<String, std::vector<uint8_t>>> files; // identifiant du système, contenu
	for (const ZipEntry &entry : entries) {
		if (entry.name == "templates/version.txt") {
			std::vector<uint8_t> text;
			if (!extract_zip_entry(data, entry, text, reason)) {
				r_message = "Paquet de modèles endommagé : " + reason + ".";
				return false;
			}
			version = str_strip(String((const char *)text.data(), text.size()));
			continue;
		}
		for (const char *id : TEMPLATE_IDS) {
			if (entry.name == String("templates/") + id + "/" + template_file_name(id)) {
				std::vector<uint8_t> content;
				if (!extract_zip_entry(data, entry, content, reason)) {
					r_message = "Paquet de modèles endommagé : " + reason + ".";
					return false;
				}
				files.push_back({ id, std::move(content) });
			}
		}
	}
	if (files.empty()) {
		r_message = "Ce fichier ne contient aucun modèle d'exportation de MEX (dossier « templates » attendu).";
		return false;
	}
	if (version != MEX_VERSION) {
		// Un jeu exporté avec un modèle d'une autre version peut ne pas lire son archive.
		r_message = "Ce paquet est fait pour MEX " + (version.empty() ? String("(version inconnue)") : version) + ", cet éditeur est en version " + MEX_VERSION + " : il n'a pas été installé. Téléchargez le paquet de la même version.";
		return false;
	}
	std::error_code ec;
	String installed;
	for (const auto &file : files) {
		const fs::path folder = fs::u8path(dir) / file.first;
		fs::create_directories(folder, ec);
		const fs::path target = folder / template_file_name(file.first);
		std::ofstream out(target, std::ios::binary | std::ios::trunc);
		out.write((const char *)file.second.data(), (std::streamsize)file.second.size());
		out.close();
		if (!out) {
			r_message = "Écriture impossible : " + target.u8string();
			return false;
		}
		fs::permissions(target, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
		installed += (installed.empty() ? "" : ", ") + file.first;
	}
	std::ofstream stamp(fs::u8path(dir) / "version.txt", std::ios::binary | std::ios::trunc);
	stamp << version << "\n";
	r_message = "Modèles de MEX " + version + " installés : " + installed + ".";
	return true;
}

bool EditorExport::remove_installed_templates(String &r_message) {
	const String dir = get_installed_templates_dir();
	std::error_code ec;
	int removed = 0;
	for (const char *id : TEMPLATE_IDS) {
		const fs::path target = fs::u8path(dir) / id / template_file_name(id);
		if (fs::remove(target, ec)) {
			removed++;
		}
		fs::remove(fs::u8path(dir) / id, ec); // le dossier, s'il est vide
	}
	fs::remove(fs::u8path(dir) / "version.txt", ec);
	r_message = removed > 0 ? "Modèles installés supprimés (" + std::to_string(removed) + ")." : String("Aucun modèle installé à supprimer.");
	return removed > 0;
}

std::vector<EditorExport::TemplateInfo> EditorExport::get_template_report() {
	std::vector<TemplateInfo> report;
	const String installed = get_installed_templates_dir();
	const String editor = OS::get_singleton()->get_executable_path();
	for (const String &platform : get_platforms()) {
		TemplateInfo info;
		info.platform = platform;
		info.path = find_template(platform);
		if (info.path.empty()) {
			info.origin = "absent";
		} else if (!installed.empty() && str_begins_with(info.path, installed)) {
			const String version = get_installed_templates_version();
			info.origin = version.empty() ? String("installé à la main") : "paquet installé (MEX " + version + ")";
		} else if (info.path.find("modeles_incorpores") != String::npos) {
			info.origin = "incorporé à l'éditeur";
		} else if (info.path == editor) {
			info.origin = "l'éditeur lui-même";
		} else {
			info.origin = "à côté de l'éditeur";
		}
		report.push_back(info);
	}
	return report;
}
