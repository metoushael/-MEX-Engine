// Resource : donnée partagée et chargeable (textures, scènes, scripts, formes...).
#pragma once

#include "core/object/ref_counted.h"

class Resource : public RefCounted {
	GDCLASS(Resource, RefCounted)

	String path;
	String name;

protected:
	static void _bind_methods();

public:
	void set_path(const String &p_path); // enregistre aussi dans le cache
	String get_path() const { return path; }
	void set_name(const String &p_name) { name = p_name; }
	String get_name() const { return name; }

	void emit_changed();
	virtual Ref<Resource> duplicate() const;

	virtual ~Resource();
};

class ResourceCache {
	friend class Resource;
	static std::unordered_map<String, Resource *> resources;

public:
	static Ref<Resource> get(const String &p_path);
	static bool has(const String &p_path);
};
