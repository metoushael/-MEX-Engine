// Chargement des ressources par extension, avec cache.
#pragma once

#include "core/io/resource.h"

class ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const = 0;
	virtual String get_resource_type(const String &p_path) const = 0;
	virtual Ref<Resource> load(const String &p_path, Error *r_error) = 0;
	virtual ~ResourceFormatLoader() {}
};

class ResourceLoader {
	static std::vector<ResourceFormatLoader *> loaders;
	static ResourceFormatLoader *_find_loader(const String &p_path);

public:
	static void add_loader(ResourceFormatLoader *p_loader); // prend possession du pointeur
	static Ref<Resource> load(const String &p_path, Error *r_error = nullptr, bool p_no_cache = false);
	static bool exists(const String &p_path);
	static String get_resource_type(const String &p_path); // "" si non reconnu
	static void get_extensions_for_type(const String &p_type, std::vector<String> *p_extensions);
	static void cleanup();
};
