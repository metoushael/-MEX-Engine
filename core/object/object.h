// Object : classe de base de tout le moteur (réflexion, signaux, scripts).
#pragma once

#include "core/variant/variant.h"

class ClassDB;
struct ClassInfo;
class ScriptInstance;

enum PropertyHint {
	PROPERTY_HINT_NONE,
	PROPERTY_HINT_RANGE, // "min,max,pas"
	PROPERTY_HINT_ENUM, // "A,B,C"
	PROPERTY_HINT_FILE, // "png,jpg"
	PROPERTY_HINT_MULTILINE_TEXT,
	PROPERTY_HINT_RESOURCE_TYPE, // "Texture2D"
	PROPERTY_HINT_RADIANS, // affiché en degrés dans l'éditeur
};

enum PropertyUsageFlags {
	PROPERTY_USAGE_NONE = 0,
	PROPERTY_USAGE_STORAGE = 1, // sauvegardé dans les scènes
	PROPERTY_USAGE_EDITOR = 2, // visible dans l'inspecteur
	PROPERTY_USAGE_CATEGORY = 4,
	PROPERTY_USAGE_GROUP = 8,
	PROPERTY_USAGE_SCRIPT_VARIABLE = 16,
	PROPERTY_USAGE_DEFAULT = PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR,
	PROPERTY_USAGE_NO_EDITOR = PROPERTY_USAGE_STORAGE,
};

struct PropertyInfo {
	Variant::Type type = Variant::NIL;
	String name;
	PropertyHint hint = PROPERTY_HINT_NONE;
	String hint_string;
	uint32_t usage = PROPERTY_USAGE_DEFAULT;

	PropertyInfo() {}
	PropertyInfo(Variant::Type p_type, const String &p_name, PropertyHint p_hint = PROPERTY_HINT_NONE, const String &p_hint_string = "", uint32_t p_usage = PROPERTY_USAGE_DEFAULT) :
			type(p_type), name(p_name), hint(p_hint), hint_string(p_hint_string), usage(p_usage) {}
};

struct MethodInfo {
	String name;
	std::vector<PropertyInfo> arguments;

	MethodInfo() {}
	MethodInfo(const String &p_name) :
			name(p_name) {}
	MethodInfo(const String &p_name, const PropertyInfo &a1) :
			name(p_name), arguments({ a1 }) {}
	MethodInfo(const String &p_name, const PropertyInfo &a1, const PropertyInfo &a2) :
			name(p_name), arguments({ a1, a2 }) {}
	MethodInfo(const String &p_name, const PropertyInfo &a1, const PropertyInfo &a2, const PropertyInfo &a3) :
			name(p_name), arguments({ a1, a2, a3 }) {}
};

// Macro à placer en tête de chaque classe dérivée d'Object (équivalent du GDCLASS de Godot).
#define GDCLASS(m_class, m_inherits)                                             \
private:                                                                         \
	friend class ClassDB;                                                        \
                                                                                 \
public:                                                                          \
	typedef m_class self_type;                                                   \
	typedef m_inherits super_type;                                               \
	static const char *get_class_static() { return #m_class; }                   \
	static const char *get_parent_class_static() { return #m_inherits; }         \
	virtual String get_class() const override { return #m_class; }               \
	virtual bool is_class(const String &p_class) const override {                \
		return p_class == #m_class || m_inherits::is_class(p_class);             \
	}                                                                            \
	static void initialize_class() {                                             \
		static bool initialized = false;                                         \
		if (initialized) {                                                       \
			return;                                                              \
		}                                                                        \
		initialized = true;                                                      \
		m_inherits::initialize_class();                                          \
		ClassDB::_add_class<m_class>();                                          \
		if (m_class::_get_bind_methods() != m_inherits::_get_bind_methods()) {   \
			_bind_methods();                                                     \
		}                                                                        \
	}                                                                            \
                                                                                 \
protected:                                                                       \
	static void (*_get_bind_methods())() { return &m_class::_bind_methods; }     \
	void (Object::*_get_notification() const)(int) {                             \
		return (void (Object::*)(int)) & m_class::_notification;                 \
	}                                                                            \
	virtual void _notificationv(int p_what, bool p_reversed) override {          \
		if (!p_reversed) {                                                       \
			m_inherits::_notificationv(p_what, p_reversed);                      \
		}                                                                        \
		if (m_class::_get_notification() != m_inherits::_get_notification()) {   \
			_notification(p_what);                                               \
		}                                                                        \
		if (p_reversed) {                                                        \
			m_inherits::_notificationv(p_what, p_reversed);                      \
		}                                                                        \
	}                                                                            \
                                                                                 \
private:

class Object {
public:
	enum {
		NOTIFICATION_POSTINITIALIZE = 0,
		NOTIFICATION_PREDELETE = 1,
	};

	enum ConnectFlags {
		CONNECT_PERSIST = 2, // sauvegardé dans les scènes
		CONNECT_ONE_SHOT = 4,
	};

	struct Connection {
		Callable callable;
		uint32_t flags = 0;
	};

private:
	friend class ClassDB;
	friend void memdelete(Object *p_object);

	ObjectID _instance_id = 0;
	mutable ClassInfo *_class_info = nullptr;
	std::map<String, std::vector<Connection>> signal_map;
	std::set<String> user_signals;
	std::map<String, Variant> metadata;
	Variant script;
	ScriptInstance *script_instance = nullptr;

protected:
	bool _is_ref_counted = false;

	virtual void _notificationv(int p_what, bool p_reversed) {}
	void _notification(int p_what) {}
	static void _bind_methods();
	static void (*_get_bind_methods())() { return &Object::_bind_methods; }
	void (Object::*_get_notification() const)(int) { return &Object::_notification; }
	virtual void _script_changed() {}

public:
	static void initialize_class();
	static const char *get_class_static() { return "Object"; }
	static const char *get_parent_class_static() { return ""; }
	virtual String get_class() const { return "Object"; }
	virtual bool is_class(const String &p_class) const { return p_class == "Object"; }
	ClassInfo *get_class_info() const;

	template <class T>
	static T *cast_to(Object *p_object) {
		return dynamic_cast<T *>(p_object);
	}
	template <class T>
	static const T *cast_to(const Object *p_object) {
		return dynamic_cast<const T *>(p_object);
	}

	ObjectID get_instance_id() const { return _instance_id; }
	bool is_ref_counted() const { return _is_ref_counted; }
	virtual String to_string() const;

	// Propriétés
	void set(const String &p_name, const Variant &p_value, bool *r_valid = nullptr);
	Variant get(const String &p_name, bool *r_valid = nullptr) const;
	void get_property_list(std::vector<PropertyInfo> *p_list) const;

	// Méthodes
	bool has_method(const String &p_method) const;
	Variant callp(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error);
	template <class... Args>
	Variant call(const String &p_method, Args... p_args) {
		Variant argv[sizeof...(p_args) + 1] = { Variant(p_args)... };
		const Variant *argp[sizeof...(p_args) + 1];
		for (size_t i = 0; i < sizeof...(p_args); i++) {
			argp[i] = &argv[i];
		}
		CallError ce;
		return callp(p_method, argp, (int)sizeof...(p_args), ce);
	}

	void notification(int p_what, bool p_reversed = false);

	// Signaux
	void add_user_signal(const String &p_name);
	bool has_signal(const String &p_name) const;
	Error connect(const String &p_signal, const Callable &p_callable, uint32_t p_flags = 0);
	void disconnect(const String &p_signal, const Callable &p_callable);
	bool is_connected(const String &p_signal, const Callable &p_callable) const;
	void emit_signalp(const String &p_name, const Variant **p_args, int p_argc);
	template <class... Args>
	void emit_signal(const String &p_name, Args... p_args) {
		Variant argv[sizeof...(p_args) + 1] = { Variant(p_args)... };
		const Variant *argp[sizeof...(p_args) + 1];
		for (size_t i = 0; i < sizeof...(p_args); i++) {
			argp[i] = &argv[i];
		}
		emit_signalp(p_name, argp, (int)sizeof...(p_args));
	}
	void get_signal_connection_list(const String &p_signal, std::vector<Connection> *p_list) const;

	// Script
	void set_script(const Variant &p_script);
	Variant get_script() const { return script; }
	ScriptInstance *get_script_instance() const { return script_instance; }

	// Métadonnées libres
	void set_meta(const String &p_name, const Variant &p_value);
	Variant get_meta(const String &p_name, const Variant &p_default = Variant()) const;
	bool has_meta(const String &p_name) const;

	Object();
	virtual ~Object();

private:
	Variant _emit_signal_bind(const Variant **p_args, int p_argc, CallError &r_error);
	Variant _call_bind(const Variant **p_args, int p_argc, CallError &r_error);
	Error _connect_bind(const String &p_signal, const Callable &p_callable, int p_flags);
};

// Base de données des instances vivantes (permet de détecter les objets libérés).
class ObjectDB {
	static std::unordered_map<ObjectID, Object *> instances;
	static ObjectID next_id;

public:
	static ObjectID add_instance(Object *p_object);
	static void remove_instance(Object *p_object);
	static Object *get_instance(ObjectID p_id) {
		auto it = instances.find(p_id);
		return it == instances.end() ? nullptr : it->second;
	}
	static int get_object_count() { return (int)instances.size(); }
};

#define memnew(m_class) (new m_class)
void memdelete(Object *p_object); // envoie NOTIFICATION_PREDELETE puis détruit l'objet

#include "core/object/class_db.h"
