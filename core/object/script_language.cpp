#include "script_language.h"

bool ScriptServer::scripting_enabled = true;

void Script::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_source_code"), &Script::get_source_code);
	ClassDB::bind_method(D_METHOD("get_instance_base_type"), &Script::get_instance_base_type);
}

ScriptInstance *Script::placeholder_instance_create(Object *p_owner) {
	return new PlaceHolderScriptInstance(Ref<Script>(this));
}

bool PlaceHolderScriptInstance::set(const String &p_name, const Variant &p_value) {
	// On accepte toute valeur : le script peut ne pas (encore) compiler.
	values[p_name] = p_value;
	return true;
}

bool PlaceHolderScriptInstance::get(const String &p_name, Variant &r_value) const {
	auto it = values.find(p_name);
	if (it != values.end()) {
		// N'expose la valeur que si la variable existe toujours dans le script
		// (ou si le script est invalide, pour ne rien perdre à la sauvegarde).
		Variant def;
		if (!script->is_valid() || script->get_property_default_value(p_name, def)) {
			r_value = it->second;
			return true;
		}
		return false;
	}
	return script->get_property_default_value(p_name, r_value);
}

void PlaceHolderScriptInstance::get_property_list(std::vector<PropertyInfo> *p_list) const {
	if (script->is_valid()) {
		script->get_script_property_list(p_list);
	} else {
		for (auto &kv : values) {
			p_list->push_back(PropertyInfo(kv.second.get_type(), kv.first, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_SCRIPT_VARIABLE));
		}
	}
}

Variant PlaceHolderScriptInstance::callp(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error) {
	r_error.error = CallError::CALL_ERROR_INVALID_METHOD;
	return Variant();
}
