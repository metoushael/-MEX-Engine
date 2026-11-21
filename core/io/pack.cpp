#include "pack.h"

#include "core/string/ustring.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace {

struct PackEntry {
	uint64_t offset = 0;
	uint64_t size = 0;
};

const char PACK_MAGIC[8] = { 'M', 'E', 'X', 'P', 'A', 'C', 'K', '1' };

String pack_path;
std::map<String, PackEntry> pack_files;
std::set<String> pack_dirs; // "" = racine

// « res://a/./b » -> « a/b »
String _normalize(const String &p_path) {
	String path = str_replace(p_path, "\\", "/");
	if (str_begins_with(path, "res://")) {
		path = path.substr(6);
	}
	std::vector<String> parts;
	for (const String &part : str_split(path, "/", false)) {
		if (part == ".") {
			continue;
		}
		if (part == "..") {
			if (!parts.empty()) {
				parts.pop_back();
			}
			continue;
		}
		parts.push_back(part);
	}
	String out;
	for (size_t i = 0; i < parts.size(); i++) {
		out += (i ? "/" : "") + parts[i];
	}
	return out;
}

template <typename T>
void _write_int(std::ostream &p_out, T p_value) {
	unsigned char bytes[sizeof(T)];
	for (size_t i = 0; i < sizeof(T); i++) {
		bytes[i] = (unsigned char)((p_value >> (8 * i)) & 0xFF);
	}
	p_out.write((const char *)bytes, sizeof(T));
}

template <typename T>
bool _read_int(std::istream &p_in, T &r_value) {
	unsigned char bytes[sizeof(T)];
	if (!p_in.read((char *)bytes, sizeof(T))) {
		return false;
	}
	r_value = 0;
	for (size_t i = 0; i < sizeof(T); i++) {
		r_value |= (T)((T)bytes[i] << (8 * i));
	}
	return true;
}

} // namespace

bool PackedData::is_loaded() {
	return !pack_path.empty();
}

String PackedData::get_pack_path() {
	return pack_path;
}

void PackedData::unload() {
	pack_path.clear();
	pack_files.clear();
	pack_dirs.clear();
}

Error PackedData::load(const String &p_pack_path) {
	std::ifstream in(fs::u8path(p_pack_path), std::ios::binary);
	if (!in) {
		return ERR_FILE_CANT_OPEN;
	}
	char magic[8];
	if (!in.read(magic, 8) || memcmp(magic, PACK_MAGIC, 8) != 0) {
		return ERR_INVALID_DATA;
	}
	in.seekg(0, std::ios::end);
	const uint64_t total_size = (uint64_t)in.tellg();
	in.seekg(8, std::ios::beg);

	uint32_t count = 0;
	if (!_read_int(in, count)) {
		return ERR_INVALID_DATA;
	}
	std::map<String, PackEntry> files;
	std::set<String> dirs;
	dirs.insert("");
	for (uint32_t i = 0; i < count; i++) {
		uint16_t length = 0;
		if (!_read_int(in, length)) {
			return ERR_INVALID_DATA;
		}
		String path(length, '\0');
		PackEntry entry;
		if (!in.read(path.data(), length) || !_read_int(in, entry.offset) || !_read_int(in, entry.size)) {
			return ERR_INVALID_DATA;
		}
		if (entry.offset > total_size || entry.size > total_size - entry.offset) {
			return ERR_INVALID_DATA; // archive tronquée
		}
		path = _normalize(path);
		files[path] = entry;
		for (size_t slash = path.find('/'); slash != String::npos; slash = path.find('/', slash + 1)) {
			dirs.insert(path.substr(0, slash));
		}
	}
	pack_path = p_pack_path;
	pack_files = std::move(files);
	pack_dirs = std::move(dirs);
	return OK;
}

bool PackedData::has_file(const String &p_path) {
	return pack_files.count(_normalize(p_path)) > 0;
}

bool PackedData::has_dir(const String &p_path) {
	return pack_dirs.count(_normalize(p_path)) > 0;
}

bool PackedData::get_file(const String &p_path, std::vector<uint8_t> *r_data) {
	auto it = pack_files.find(_normalize(p_path));
	if (it == pack_files.end()) {
		return false;
	}
	std::ifstream in(fs::u8path(pack_path), std::ios::binary);
	if (!in) {
		return false;
	}
	r_data->resize((size_t)it->second.size);
	in.seekg((std::streamoff)it->second.offset, std::ios::beg);
	if (it->second.size > 0 && !in.read((char *)r_data->data(), (std::streamsize)it->second.size)) {
		r_data->clear();
		return false;
	}
	return true;
}

void PackedData::list_dir(const String &p_path, std::vector<String> *r_files, std::vector<String> *r_dirs) {
	const String dir = _normalize(p_path);
	const String prefix = dir.empty() ? String() : dir + "/";
	if (r_files) {
		for (const auto &kv : pack_files) {
			if (str_begins_with(kv.first, prefix) && kv.first.find('/', prefix.size()) == String::npos) {
				r_files->push_back(kv.first.substr(prefix.size()));
			}
		}
	}
	if (r_dirs) {
		for (const String &d : pack_dirs) {
			if (!d.empty() && str_begins_with(d, prefix) && d.size() > prefix.size() && d.find('/', prefix.size()) == String::npos) {
				r_dirs->push_back(d.substr(prefix.size()));
			}
		}
	}
}

Error PackedData::create(const String &p_pack_path, const std::vector<std::pair<String, String>> &p_files) {
	// Table des matières d'abord (sa taille fixe la position des données), puis les fichiers.
	struct Item {
		String name;
		fs::path source;
		uint64_t size = 0;
	};
	std::vector<Item> items;
	uint64_t header_size = 8 + 4;
	for (const auto &file : p_files) {
		Item item;
		item.name = _normalize(file.first);
		item.source = fs::u8path(file.second);
		std::error_code ec;
		item.size = (uint64_t)fs::file_size(item.source, ec);
		if (ec || item.name.empty() || item.name.size() > 0xFFFF) {
			return ERR_FILE_CANT_OPEN;
		}
		header_size += 2 + item.name.size() + 8 + 8;
		items.push_back(item);
	}

	std::ofstream out(fs::u8path(p_pack_path), std::ios::binary | std::ios::trunc);
	if (!out) {
		return ERR_FILE_CANT_WRITE;
	}
	out.write(PACK_MAGIC, 8);
	_write_int(out, (uint32_t)items.size());
	uint64_t offset = header_size;
	for (const Item &item : items) {
		_write_int(out, (uint16_t)item.name.size());
		out.write(item.name.data(), (std::streamsize)item.name.size());
		_write_int(out, offset);
		_write_int(out, item.size);
		offset += item.size;
	}
	std::vector<char> buffer(1 << 16);
	for (const Item &item : items) {
		std::ifstream in(item.source, std::ios::binary);
		if (!in) {
			return ERR_FILE_CANT_OPEN;
		}
		uint64_t remaining = item.size;
		while (remaining > 0) {
			const std::streamsize chunk = (std::streamsize)std::min<uint64_t>(remaining, buffer.size());
			if (!in.read(buffer.data(), chunk)) {
				return ERR_FILE_CANT_OPEN; // le fichier a changé pendant l'export
			}
			out.write(buffer.data(), chunk);
			remaining -= (uint64_t)chunk;
		}
	}
	out.flush();
	return out.good() ? OK : ERR_FILE_CANT_WRITE;
}
