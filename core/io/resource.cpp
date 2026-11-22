#include "resource.h"

std::unordered_map<String, Resource *> ResourceCache::resources;

Ref<Resource> ResourceCache::get(const String &p_path) {
	auto it = resources.find(p_path);
	return it == resources.end() ? Ref<Resource>() : Ref<Resource>(it->second);
}

bool ResourceCache::has(const String &p_path) {
	return resources.count(p_path) > 0;
}

void Resource::set_path(const String &p_path) {
	if (path == p_path) {
		return;
	}
	if (!path.empty()) {
		auto it = ResourceCache::resources.find(path);
		if (it != ResourceCache::resources.end() && it->second == this) {
			ResourceCache::resources.erase(it);
		}
	}
	path = p_path;
	if (!path.empty()) {
		ResourceCache::resources[path] = this;
	}
}

void Resource::emit_changed() {
	emit_signal("changed");
}

Ref<Resource> Resource::duplicate() const {
	Object *obj = ClassDB::instantiate(get_class());
	Resource *res = Object::cast_to<Resource>(obj);
	if (!res) {
		if (obj) {
			memdelete(obj);
		}
		return Ref<Resource>();
	}
	Ref<Resource> ref(res);
	std::vector<PropertyInfo> props;
	get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (pi.usage & PROPERTY_USAGE_STORAGE) {
			res->set(pi.name, get(pi.name));
		}
	}
	return ref;
}

Resource::~Resource() {
	if (!path.empty()) {
		auto it = ResourceCache::resources.find(path);
		if (it != ResourceCache::resources.end() && it->second == this) {
			ResourceCache::resources.erase(it);
		}
	}
}

void Resource::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_path", "path"), &Resource::set_path);
	ClassDB::bind_method(D_METHOD("get_path"), &Resource::get_path);
	ClassDB::bind_method(D_METHOD("set_name", "name"), &Resource::set_name);
	ClassDB::bind_method(D_METHOD("get_name"), &Resource::get_name);
	ClassDB::bind_method(D_METHOD("duplicate"), &Resource::duplicate);
	ClassDB::bind_method(D_METHOD("emit_changed"), &Resource::emit_changed);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "resource_name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_name", "get_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "resource_path", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_path", "get_path");

	ADD_SIGNAL(MethodInfo("changed"));
}
