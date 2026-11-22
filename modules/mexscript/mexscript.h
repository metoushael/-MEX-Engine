// MexScript : langage de script du moteur (syntaxe proche de GDScript), interprété
// directement sur l'arbre syntaxique.
#pragma once

#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "modules/mexscript/mexscript_parser.h"

class MexScriptInstance;

// Référence à une classe native dans un script : `Node2D.new()`, `Node.NOTIFICATION_READY`.
class MexScriptNativeClass : public RefCounted {
	GDCLASS(MexScriptNativeClass, RefCounted)

	String name;

protected:
	static void _bind_methods() {}

public:
	void set_name(const String &p_name) { name = p_name; }
	String get_name() const { return name; }
	virtual String to_string() const override { return "<Classe " + name + ">"; }
};

class MexScript : public Script {
	GDCLASS(MexScript, Script)

	friend class MexScriptInstance;
	friend struct MXInterpreter;

	struct MemberInfo {
		PropertyInfo info;
		Variant default_value;
		Variant::Type type = Variant::NIL; // type déclaré (NIL = dynamique)
		bool exported = false;
	};

	String source;
	MXParseResult parsed;
	bool valid = false;
	std::unordered_map<String, Variant> constants;
	std::unordered_map<String, int> member_indices;
	std::vector<MemberInfo> member_info; // aligné sur parsed.members
	std::set<String> signal_names;
	bool has_onready = false;
	std::set<MexScriptInstance *> instances;
	String debug_path;
	String last_runtime_error;

	static Variant _call_lambda(const Ref<MexScript> &p_script, ObjectID p_owner, int p_function, const std::vector<std::pair<String, Variant>> &p_captures, const Variant **p_args, int p_argc);

protected:
	static void _bind_methods();

public:
	virtual ScriptInstance *instance_create(Object *p_owner) override;

	virtual String get_source_code() const override { return source; }
	virtual void set_source_code(const String &p_code) override { source = p_code; }
	virtual Error reload() override;
	virtual bool is_valid() const override { return valid; }

	virtual String get_instance_base_type() const override { return parsed.extends; }
	virtual bool has_method(const String &p_method) const override;
	virtual bool has_script_signal(const String &p_signal) const override { return signal_names.count(p_signal) > 0; }
	virtual void get_script_signal_list(std::vector<MethodInfo> *p_list) const override;
	virtual void get_script_property_list(std::vector<PropertyInfo> *p_list) const override;
	virtual bool get_property_default_value(const String &p_name, Variant &r_value) const override;

	void get_method_list(std::vector<String> *p_list) const;
	// Dernière erreur de compilation (vide si le script est valide).
	String get_error_message() const { return parsed.has_error ? parsed.error : String(); }
	int get_error_line() const { return parsed.error_line; }

	void set_debug_path(const String &p_path) { debug_path = p_path; }
	String get_debug_path() const;
	void report_runtime_error(int p_line, const String &p_function, const String &p_message);

	// Crée un objet du type de base et lui attache ce script (`Script.new()`).
	Variant instantiate_object();
};

class MexScriptInstance : public ScriptInstance {
	friend class MexScript;
	friend struct MXInterpreter;

	Object *owner = nullptr;
	Ref<MexScript> script;
	std::vector<Variant> members; // aligné sur script->parsed.members
	bool onready_done = false;

	void _init_members(bool p_onready);

public:
	virtual bool set(const String &p_name, const Variant &p_value) override;
	virtual bool get(const String &p_name, Variant &r_value) const override;
	virtual void get_property_list(std::vector<PropertyInfo> *p_list) const override;
	virtual bool has_method(const String &p_method) const override;
	virtual Variant callp(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error) override;
	virtual Ref<Script> get_script() const override { return script; }

	Object *get_owner() const { return owner; }

	virtual ~MexScriptInstance();
};

class ResourceFormatLoaderMexScript : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override { p_extensions->push_back("mx"); }
	virtual String get_resource_type(const String &p_path) const override { return "MexScript"; }
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};

// Fonctions globales (print, lerp, range...).
struct MXBuiltinContext {
	String error; // renseigné en cas d'erreur
};
typedef Variant (*MXBuiltinFunction)(const Variant **p_args, int p_argc, MXBuiltinContext &r_ctx);
MXBuiltinFunction mexscript_find_builtin(const String &p_name);
void mexscript_get_builtin_names(std::vector<String> *r_names);
bool mexscript_find_global_constant(const String &p_name, Variant &r_value);

void register_mexscript_types();
void unregister_mexscript_types();
