#include "resource_loader.h"

#include "core/io/file_access.h"
#include "core/string/ustring.h"

std::vector<ResourceFormatLoader *> ResourceLoader::loaders;

void ResourceLoader::add_loader(ResourceFormatLoader *p_loader) {
	loaders.push_back(p_loader);
}

ResourceFormatLoader *ResourceLoader::_find_loader(const String &p_path) {
	String ext = path_get_extension(p_path);
	for (ResourceFormatLoader *loader : loaders) {
		std::vector<String> exts;
		loader->get_recognized_extensions(&exts);
		if (std::find(exts.begin(), exts.end(), ext) != exts.end()) {
			return loader;
		}
	}
	return nullptr;
}

Ref<Resource> ResourceLoader::load(const String &p_path, Error *r_error, bool p_no_cache) {
	if (r_error) {
		*r_error = OK;
	}
	String path = path_simplify(p_path);
	if (!p_no_cache) {
		Ref<Resource> cached = ResourceCache::get(path);
		if (cached.is_valid()) {
			return cached;
		}
	}
	ResourceFormatLoader *loader = _find_loader(path);
	if (!loader) {
		print_error("Impossible de charger '" + path + "' : format non reconnu.");
		if (r_error) {
			*r_error = ERR_UNAVAILABLE;
		}
		return Ref<Resource>();
	}
	if (!FileAccess::exists(path)) {
		print_error("Impossible de charger '" + path + "' : fichier introuvable.");
		if (r_error) {
			*r_error = ERR_FILE_NOT_FOUND;
		}
		return Ref<Resource>();
	}
	Error err = OK;
	Ref<Resource> res = loader->load(path, &err);
	if (res.is_null()) {
		if (r_error) {
			*r_error = err == OK ? FAILED : err;
		}
		return res;
	}
	if (!p_no_cache) {
		res->set_path(path);
	}
	return res;
}

bool ResourceLoader::exists(const String &p_path) {
	return _find_loader(p_path) != nullptr && FileAccess::exists(p_path);
}

String ResourceLoader::get_resource_type(const String &p_path) {
	ResourceFormatLoader *loader = _find_loader(p_path);
	return loader ? loader->get_resource_type(p_path) : String();
}

void ResourceLoader::get_extensions_for_type(const String &p_type, std::vector<String> *p_extensions) {
	for (ResourceFormatLoader *loader : loaders) {
		std::vector<String> exts;
		loader->get_recognized_extensions(&exts);
		for (const String &ext : exts) {
			String type = loader->get_resource_type("x." + ext);
			if (p_type.empty() || ClassDB::is_parent_class(type, p_type)) {
				p_extensions->push_back(ext);
			}
		}
	}
}

void ResourceLoader::cleanup() {
	for (ResourceFormatLoader *loader : loaders) {
		delete loader;
	}
	loaders.clear();
}
