// Interfaces de scripting : Script (ressource) et ScriptInstance (attachée à un objet).
#pragma once

#include "core/io/resource.h"

class Script;

class ScriptInstance {
public:
	virtual bool set(const String &p_name, const Variant &p_value) = 0;
	virtual bool get(const String &p_name, Variant &r_value) const = 0;
	virtual void get_property_list(std::vector<PropertyInfo> *p_list) const = 0;
	virtual bool has_method(const String &p_method) const = 0;
	virtual Variant callp(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error) = 0;
	virtual Ref<Script> get_script() const = 0;
	virtual bool is_placeholder() const { return false; }
	virtual ~ScriptInstance() {}
};

class Script : public Resource {
	GDCLASS(Script, Resource)

protected:
	static void _bind_methods();

public:
	virtual ScriptInstance *instance_create(Object *p_owner) = 0;
	virtual ScriptInstance *placeholder_instance_create(Object *p_owner);

	virtual String get_source_code() const = 0;
	virtual void set_source_code(const String &p_code) = 0;
	virtual Error reload() = 0; // recompile ; les erreurs sont journalisées
	virtual bool is_valid() const = 0;

	virtual String get_instance_base_type() const = 0;
	virtual bool has_method(const String &p_method) const = 0;
	virtual bool has_script_signal(const String &p_signal) const = 0;
	virtual void get_script_signal_list(std::vector<MethodInfo> *p_list) const = 0;
	virtual void get_script_property_list(std::vector<PropertyInfo> *p_list) const = 0;
	virtual bool get_property_default_value(const String &p_name, Variant &r_value) const = 0;
};

// Instance "fantôme" utilisée dans l'éditeur : le code ne tourne pas, mais les
// variables exportées restent éditables et sauvegardées.
class PlaceHolderScriptInstance : public ScriptInstance {
	Ref<Script> script;
	std::map<String, Variant> values;

public:
	virtual bool set(const String &p_name, const Variant &p_value) override;
	virtual bool get(const String &p_name, Variant &r_value) const override;
	virtual void get_property_list(std::vector<PropertyInfo> *p_list) const override;
	virtual bool has_method(const String &p_method) const override { return false; }
	virtual Variant callp(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error) override;
	virtual Ref<Script> get_script() const override { return script; }
	virtual bool is_placeholder() const override { return true; }

	PlaceHolderScriptInstance(const Ref<Script> &p_script) :
			script(p_script) {}
};

class ScriptServer {
	static bool scripting_enabled;

public:
	// Désactivé par l'éditeur quand il charge une scène pour l'édition.
	static void set_scripting_enabled(bool p_enabled) { scripting_enabled = p_enabled; }
	static bool is_scripting_enabled() { return scripting_enabled; }
};

// Désactive le scripting le temps d'une portée (RAII).
struct ScriptingDisabledScope {
	bool previous;
	ScriptingDisabledScope() {
		previous = ScriptServer::is_scripting_enabled();
		ScriptServer::set_scripting_enabled(false);
	}
	~ScriptingDisabledScope() { ScriptServer::set_scripting_enabled(previous); }
};
