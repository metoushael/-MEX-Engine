#include "file_access.h"

#include "core/config/project_settings.h"
#include "core/io/pack.h"
#include "core/string/ustring.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static fs::path _abs(const String &p_path) {
	return fs::u8path(ProjectSettings::get_singleton()->globalize_path(p_path));
}

// Vrai si le chemin doit être cherché dans l'archive du jeu exporté.
static bool _in_pack(const String &p_path) {
	return PackedData::is_loaded() && str_begins_with(p_path, "res://");
}

bool FileAccess::exists(const String &p_path) {
	if (_in_pack(p_path) && PackedData::has_file(p_path)) {
		return true;
	}
	std::error_code ec;
	return fs::is_regular_file(_abs(p_path), ec);
}

bool FileAccess::dir_exists(const String &p_path) {
	if (_in_pack(p_path) && PackedData::has_dir(p_path)) {
		return true;
	}
	std::error_code ec;
	return fs::is_directory(_abs(p_path), ec);
}

String FileAccess::get_file_as_string(const String &p_path, Error *r_error) {
	Error err = OK;
	const std::vector<uint8_t> bytes = get_file_as_bytes(p_path, &err);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return String();
	}
	String text((const char *)bytes.data(), bytes.size());
	// Retire le BOM UTF-8 et normalise les fins de ligne.
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
		text = text.substr(3);
	}
	String out;
	out.reserve(text.size());
	for (char c : text) {
		if (c != '\r') {
			out += c;
		}
	}
	return out;
}

std::vector<uint8_t> FileAccess::get_file_as_bytes(const String &p_path, Error *r_error) {
	std::vector<uint8_t> data;
	if (_in_pack(p_path) && PackedData::get_file(p_path, &data)) {
		if (r_error) {
			*r_error = OK;
		}
		return data;
	}
	std::ifstream f(_abs(p_path), std::ios::binary);
	if (!f) {
		if (r_error) {
			*r_error = ERR_FILE_CANT_OPEN;
		}
		return data;
	}
	f.seekg(0, std::ios::end);
	std::streamsize size = f.tellg();
	f.seekg(0, std::ios::beg);
	data.resize((size_t)std::max<std::streamsize>(size, 0));
	if (size > 0) {
		f.read((char *)data.data(), size);
	}
	if (r_error) {
		*r_error = OK;
	}
	return data;
}

Error FileAccess::write_string(const String &p_path, const String &p_text) {
	fs::path path = _abs(p_path);
	std::error_code ec;
	if (path.has_parent_path()) {
		fs::create_directories(path.parent_path(), ec);
	}
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) {
		return ERR_FILE_CANT_WRITE;
	}
	f.write(p_text.data(), (std::streamsize)p_text.size());
	return f.good() ? OK : ERR_FILE_CANT_WRITE;
}

Error FileAccess::write_bytes(const String &p_path, const std::vector<uint8_t> &p_data) {
	fs::path path = _abs(p_path);
	std::error_code ec;
	if (path.has_parent_path()) {
		fs::create_directories(path.parent_path(), ec);
	}
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) {
		return ERR_FILE_CANT_WRITE;
	}
	f.write((const char *)p_data.data(), (std::streamsize)p_data.size());
	return f.good() ? OK : ERR_FILE_CANT_WRITE;
}

Error FileAccess::copy(const String &p_from, const String &p_to) {
	const fs::path from = _abs(p_from);
	const fs::path to = _abs(p_to);
	std::error_code ec;
	if (!fs::is_regular_file(from, ec)) {
		return ERR_FILE_NOT_FOUND;
	}
	if (to.has_parent_path()) {
		fs::create_directories(to.parent_path(), ec);
	}
	ec.clear();
	if (fs::equivalent(from, to, ec)) {
		return OK; // même fichier
	}
	ec.clear();
	fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
	if (ec) {
		return ERR_FILE_CANT_WRITE;
	}
	fs::permissions(to, fs::status(from, ec).permissions(), ec);
	return OK;
}

Error FileAccess::make_dir_recursive(const String &p_path) {
	std::error_code ec;
	fs::create_directories(_abs(p_path), ec);
	return ec ? ERR_CANT_CREATE : OK;
}

Error FileAccess::remove(const String &p_path) {
	std::error_code ec;
	fs::remove(_abs(p_path), ec);
	return ec ? FAILED : OK;
}

Error FileAccess::rename(const String &p_from, const String &p_to) {
	std::error_code ec;
	fs::rename(_abs(p_from), _abs(p_to), ec);
	return ec ? FAILED : OK;
}

void FileAccess::list_dir(const String &p_path, std::vector<String> *r_files, std::vector<String> *r_dirs) {
	if (_in_pack(p_path) && PackedData::has_dir(p_path)) {
		PackedData::list_dir(p_path, r_files, r_dirs); // déjà trié
		return;
	}
	std::error_code ec;
	for (fs::directory_iterator it(_abs(p_path), ec), end; !ec && it != end; it.increment(ec)) {
		String name = it->path().filename().u8string();
		if (name.empty() || name[0] == '.') {
			continue;
		}
		if (it->is_directory(ec)) {
			if (r_dirs) {
				r_dirs->push_back(name);
			}
		} else if (r_files) {
			r_files->push_back(name);
		}
	}
	if (r_files) {
		std::sort(r_files->begin(), r_files->end());
	}
	if (r_dirs) {
		std::sort(r_dirs->begin(), r_dirs->end());
	}
}

uint64_t FileAccess::get_modified_time(const String &p_path) {
	std::error_code ec;
	auto t = fs::last_write_time(_abs(p_path), ec);
	return ec ? 0 : (uint64_t)t.time_since_epoch().count();
}
