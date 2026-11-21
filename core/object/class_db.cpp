#include "class_db.h"

#include "core/object/ref_counted.h"

Variant MethodBind::call(Object *p_object, const Variant **p_args, int p_argc, CallError &r_error) {
	r_error.error = CallError::CALL_OK;
	if (is_vararg) {
		return _call(p_object, p_args, p_argc, r_error);
	}
	if (p_argc > argument_count) {
		r_error.error = CallError::CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error.expected = argument_count;
		return Variant();
	}
	const int required = argument_count - (int)default_arguments.size();
	if (p_argc < required) {
		r_error.error = CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = required;
		return Variant();
	}
	if (p_argc == argument_count) {
		return _call(p_object, p_args, p_argc, r_error);
	}
	// Complète avec les valeurs par défaut.
	const Variant *full[16];
	if (argument_count > 16) {
		r_error.error = CallError::CALL_ERROR_TOO_MANY_ARGUMENTS;
		return Variant();
	}
	for (int i = 0; i < argument_count; i++) {
		full[i] = i < p_argc ? p_args[i] : &default_arguments[i - required];
	}
	return _call(p_object, full, argument_count, r_error);
}

std::unordered_map<String, ClassInfo *> &ClassDB::classes() {
	static std::unordered_map<String, ClassInfo *> map;
	return map;
}

void ClassDB::_add_class2(const String &p_class, const String &p_parent) {
	auto &map = classes();
	if (map.count(p_class)) {
		return;
	}
	ClassInfo *info = new ClassInfo;
	info->name = p_class;
	info->parent = p_parent;
	if (!p_parent.empty()) {
		auto it = map.find(p_parent);
		if (it != map.end()) {
			info->parent_ptr = it->second;
		}
	}
	map[p_class] = info;
}

MethodBind *ClassDB::_bind_methodp(MethodBind *p_bind) {
	ClassInfo *info = get_class_info(p_bind->instance_class);
	if (!info) {
		print_error("bind_method(): classe '" + p_bind->instance_class + "' non enregistrée (méthode '" + p_bind->name + "').");
		delete p_bind;
		return nullptr;
	}
	if (info->methods.count(p_bind->name)) {
		print_error("bind_method(): la méthode '" + info->name + "::" + p_bind->name + "' est déjà liée.");
		delete p_bind;
		return nullptr;
	}
	info->methods[p_bind->name] = p_bind;
	return p_bind;
}

void ClassDB::add_property(const String &p_class, const PropertyInfo &p_info, const String &p_setter, const String &p_getter) {
	ClassInfo *info = get_class_info(p_class);
	ERR_FAIL_NULL(info);
	PropertySetGet psg;
	psg.info = p_info;
	psg.setter = p_setter.empty() ? nullptr : get_method(info, p_setter);
	psg.getter = get_method(info, p_getter);
	if (!p_setter.empty() && !psg.setter) {
		print_error("ADD_PROPERTY: setter '" + p_setter + "' introuvable pour " + p_class + "." + p_info.name);
	}
	if (!psg.getter) {
		print_error("ADD_PROPERTY: getter '" + p_getter + "' introuvable pour " + p_class + "." + p_info.name);
	}
	info->property_list.push_back(p_info);
	info->property_map[p_info.name] = psg;
}

void ClassDB::add_property_group(const String &p_class, const String &p_name) {
	ClassInfo *info = get_class_info(p_class);
	ERR_FAIL_NULL(info);
	info->property_list.push_back(PropertyInfo(Variant::NIL, p_name, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_GROUP));
}

void ClassDB::add_signal(const String &p_class, const MethodInfo &p_signal) {
	ClassInfo *info = get_class_info(p_class);
	ERR_FAIL_NULL(info);
	info->signals.push_back(p_signal);
}

void ClassDB::bind_integer_constant(const String &p_class, const String &p_name, int64_t p_value) {
	ClassInfo *info = get_class_info(p_class);
	ERR_FAIL_NULL(info);
	info->constants[p_name] = p_value;
}

ClassInfo *ClassDB::get_class_info(const String &p_class) {
	auto &map = classes();
	auto it = map.find(p_class);
	return it == map.end() ? nullptr : it->second;
}

bool ClassDB::class_exists(const String &p_class) {
	return get_class_info(p_class) != nullptr;
}

bool ClassDB::can_instantiate(const String &p_class) {
	ClassInfo *info = get_class_info(p_class);
	return info && info->creator;
}

Object *ClassDB::instantiate(const String &p_class) {
	ClassInfo *info = get_class_info(p_class);
	if (!info || !info->creator) {
		return nullptr;
	}
	return info->creator();
}

bool ClassDB::is_parent_class(const String &p_class, const String &p_inherits) {
	ClassInfo *info = get_class_info(p_class);
	while (info) {
		if (info->name == p_inherits) {
			return true;
		}
		info = info->parent_ptr;
	}
	return false;
}

String ClassDB::get_parent_class(const String &p_class) {
	ClassInfo *info = get_class_info(p_class);
	return info ? info->parent : String();
}

void ClassDB::get_class_list(std::vector<String> *p_list) {
	for (auto &kv : classes()) {
		p_list->push_back(kv.first);
	}
	std::sort(p_list->begin(), p_list->end());
}

void ClassDB::get_direct_inheriters(const String &p_class, std::vector<String> *p_list) {
	for (auto &kv : classes()) {
		if (kv.second->parent == p_class) {
			p_list->push_back(kv.first);
		}
	}
	std::sort(p_list->begin(), p_list->end());
}

MethodBind *ClassDB::get_method(ClassInfo *p_info, const String &p_method) {
	while (p_info) {
		auto it = p_info->methods.find(p_method);
		if (it != p_info->methods.end()) {
			return it->second;
		}
		p_info = p_info->parent_ptr;
	}
	return nullptr;
}

bool ClassDB::has_signal(ClassInfo *p_info, const String &p_signal) {
	while (p_info) {
		for (const MethodInfo &mi : p_info->signals) {
			if (mi.name == p_signal) {
				return true;
			}
		}
		p_info = p_info->parent_ptr;
	}
	return false;
}

void ClassDB::get_signal_list(const String &p_class, std::vector<MethodInfo> *p_list) {
	ClassInfo *info = get_class_info(p_class);
	while (info) {
		for (const MethodInfo &mi : info->signals) {
			p_list->push_back(mi);
		}
		info = info->parent_ptr;
	}
}

bool ClassDB::get_integer_constant(const String &p_class, const String &p_name, int64_t &r_value) {
	ClassInfo *info = get_class_info(p_class);
	while (info) {
		auto it = info->constants.find(p_name);
		if (it != info->constants.end()) {
			r_value = it->second;
			return true;
		}
		info = info->parent_ptr;
	}
	return false;
}

void ClassDB::get_property_list(ClassInfo *p_info, std::vector<PropertyInfo> *p_list, bool p_no_inheritance) {
	if (!p_info) {
		return;
	}
	if (!p_no_inheritance && p_info->parent_ptr) {
		get_property_list(p_info->parent_ptr, p_list, false);
	}
	if (!p_info->property_list.empty()) {
		p_list->push_back(PropertyInfo(Variant::NIL, p_info->name, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CATEGORY));
		for (const PropertyInfo &pi : p_info->property_list) {
			p_list->push_back(pi);
		}
	}
}

const PropertySetGet *ClassDB::find_property(ClassInfo *p_info, const String &p_name) {
	while (p_info) {
		auto it = p_info->property_map.find(p_name);
		if (it != p_info->property_map.end()) {
			return &it->second;
		}
		p_info = p_info->parent_ptr;
	}
	return nullptr;
}

bool ClassDB::set_property(Object *p_object, const String &p_name, const Variant &p_value) {
	const PropertySetGet *psg = find_property(p_object->get_class_info(), p_name);
	if (!psg) {
		return false;
	}
	if (psg->setter) {
		const Variant *args[1] = { &p_value };
		CallError ce;
		psg->setter->call(p_object, args, 1, ce);
	}
	return true;
}

bool ClassDB::get_property(const Object *p_object, const String &p_name, Variant &r_value) {
	const PropertySetGet *psg = find_property(p_object->get_class_info(), p_name);
	if (!psg) {
		return false;
	}
	if (psg->getter) {
		CallError ce;
		r_value = psg->getter->call(const_cast<Object *>(p_object), nullptr, 0, ce);
	}
	return true;
}

Variant ClassDB::class_get_default_property_value(const String &p_class, const String &p_property, bool *r_valid) {
	if (r_valid) {
		*r_valid = false;
	}
	ClassInfo *info = get_class_info(p_class);
	if (!info || !info->creator) {
		return Variant();
	}
	if (!info->default_instance) {
		info->default_instance = info->creator();
		// Les RefCounted par défaut sont retenus pour toute la durée du programme.
		if (info->default_instance->is_ref_counted()) {
			static_cast<RefCounted *>(info->default_instance)->reference();
		}
	}
	Variant ret;
	if (get_property(info->default_instance, p_property, ret)) {
		if (r_valid) {
			*r_valid = true;
		}
	}
	return ret;
}

void ClassDB::cleanup() {
	auto &map = classes();
	for (auto &kv : map) {
		if (kv.second->default_instance) {
			memdelete(kv.second->default_instance);
			kv.second->default_instance = nullptr;
		}
	}
	for (auto &kv : map) {
		for (auto &m : kv.second->methods) {
			delete m.second;
		}
		delete kv.second;
	}
	map.clear();
}
