// ClassDB : registre des classes, méthodes, propriétés et signaux (réflexion).
#pragma once

#include "core/object/object.h"

#include <utility>

// ---------------------------------------------------------------------------
// Conversion Variant -> type C++ pour les liaisons de méthodes
// ---------------------------------------------------------------------------
template <class T, class Enable = void>
struct VariantCaster;

template <>
struct VariantCaster<Variant> {
	static Variant cast(const Variant &v) { return v; }
};
template <>
struct VariantCaster<bool> {
	static bool cast(const Variant &v) { return v.as_bool(); }
};
template <class T>
struct VariantCaster<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
	static T cast(const Variant &v) { return (T)v.as_int(); }
};
template <class T>
struct VariantCaster<T, std::enable_if_t<std::is_floating_point_v<T>>> {
	static T cast(const Variant &v) { return (T)v.as_float(); }
};
template <class T>
struct VariantCaster<T, std::enable_if_t<std::is_enum_v<T>>> {
	static T cast(const Variant &v) { return (T)v.as_int(); }
};
template <>
struct VariantCaster<String> {
	static String cast(const Variant &v) { return v.as_string(); }
};
template <>
struct VariantCaster<Vector2> {
	static Vector2 cast(const Variant &v) { return v.as_vector2(); }
};
template <>
struct VariantCaster<Vector3> {
	static Vector3 cast(const Variant &v) { return v.as_vector3(); }
};
template <>
struct VariantCaster<Rect2> {
	static Rect2 cast(const Variant &v) { return v.as_rect2(); }
};
template <>
struct VariantCaster<Color> {
	static Color cast(const Variant &v) { return v.as_color(); }
};
template <>
struct VariantCaster<Array> {
	static Array cast(const Variant &v) { return v.as_array(); }
};
template <>
struct VariantCaster<Dictionary> {
	static Dictionary cast(const Variant &v) { return v.as_dictionary(); }
};
template <>
struct VariantCaster<Callable> {
	static Callable cast(const Variant &v) { return v.as_callable(); }
};
template <>
struct VariantCaster<Signal> {
	static Signal cast(const Variant &v) { return v.as_signal(); }
};
template <class T>
struct VariantCaster<T *, void> {
	static T *cast(const Variant &v) { return Object::cast_to<std::remove_const_t<T>>(v.as_object()); }
};
template <class T>
struct VariantCaster<Ref<T>, void> {
	static Ref<T> cast(const Variant &v) { return Ref<T>(Object::cast_to<T>(v.as_object())); }
};

// ---------------------------------------------------------------------------
// MethodBind : enveloppe appelable d'une méthode C++
// ---------------------------------------------------------------------------
class MethodBind {
public:
	String name;
	String instance_class;
	int argument_count = 0;
	std::vector<Variant> default_arguments;
	bool is_vararg = false;

	Variant call(Object *p_object, const Variant **p_args, int p_argc, CallError &r_error);
	virtual ~MethodBind() {}

protected:
	virtual Variant _call(Object *p_object, const Variant **p_args, int p_argc, CallError &r_error) = 0;
};

template <class T, class R, class... P>
class MethodBindT : public MethodBind {
	R (T::*method)(P...);

	template <size_t... I>
	Variant _invoke(T *p_instance, const Variant **p_args, std::index_sequence<I...>) {
		(void)p_args;
		if constexpr (std::is_void_v<R>) {
			(p_instance->*method)(VariantCaster<std::decay_t<P>>::cast(*p_args[I])...);
			return Variant();
		} else {
			return Variant((p_instance->*method)(VariantCaster<std::decay_t<P>>::cast(*p_args[I])...));
		}
	}

protected:
	virtual Variant _call(Object *p_object, const Variant **p_args, int p_argc, CallError &r_error) override {
		return _invoke(static_cast<T *>(p_object), p_args, std::index_sequence_for<P...>{});
	}

public:
	MethodBindT(R (T::*p_method)(P...)) :
			method(p_method) {
		argument_count = sizeof...(P);
		instance_class = T::get_class_static();
	}
};

template <class T, class R, class... P>
class MethodBindTC : public MethodBind {
	R (T::*method)(P...) const;

	template <size_t... I>
	Variant _invoke(const T *p_instance, const Variant **p_args, std::index_sequence<I...>) {
		(void)p_args;
		if constexpr (std::is_void_v<R>) {
			(p_instance->*method)(VariantCaster<std::decay_t<P>>::cast(*p_args[I])...);
			return Variant();
		} else {
			return Variant((p_instance->*method)(VariantCaster<std::decay_t<P>>::cast(*p_args[I])...));
		}
	}

protected:
	virtual Variant _call(Object *p_object, const Variant **p_args, int p_argc, CallError &r_error) override {
		return _invoke(static_cast<const T *>(p_object), p_args, std::index_sequence_for<P...>{});
	}

public:
	MethodBindTC(R (T::*p_method)(P...) const) :
			method(p_method) {
		argument_count = sizeof...(P);
		instance_class = T::get_class_static();
	}
};

template <class T>
class MethodBindVarArg : public MethodBind {
	Variant (T::*method)(const Variant **, int, CallError &);

protected:
	virtual Variant _call(Object *p_object, const Variant **p_args, int p_argc, CallError &r_error) override {
		return (static_cast<T *>(p_object)->*method)(p_args, p_argc, r_error);
	}

public:
	MethodBindVarArg(Variant (T::*p_method)(const Variant **, int, CallError &)) :
			method(p_method) {
		is_vararg = true;
		instance_class = T::get_class_static();
	}
};

template <class T, class R, class... P>
MethodBind *create_method_bind(R (T::*p_method)(P...)) {
	return new MethodBindT<T, R, P...>(p_method);
}
template <class T, class R, class... P>
MethodBind *create_method_bind(R (T::*p_method)(P...) const) {
	return new MethodBindTC<T, R, P...>(p_method);
}

// ---------------------------------------------------------------------------
// ClassDB
// ---------------------------------------------------------------------------
struct PropertySetGet {
	PropertyInfo info;
	MethodBind *setter = nullptr;
	MethodBind *getter = nullptr;
};

struct ClassInfo {
	String name;
	String parent;
	ClassInfo *parent_ptr = nullptr;
	Object *(*creator)() = nullptr;
	std::unordered_map<String, MethodBind *> methods;
	std::vector<PropertyInfo> property_list; // ordre de déclaration (inclut les groupes)
	std::unordered_map<String, PropertySetGet> property_map;
	std::vector<MethodInfo> signals;
	std::unordered_map<String, int64_t> constants;
	Object *default_instance = nullptr; // pour les valeurs par défaut des propriétés
};

#define D_METHOD(m_name, ...) m_name
#define DEFVAL(m_val) Variant(m_val)

class ClassDB {
	static std::unordered_map<String, ClassInfo *> &classes();

	template <class T>
	static Object *_creator() {
		return new T;
	}

	static void _add_class2(const String &p_class, const String &p_parent);
	static MethodBind *_bind_methodp(MethodBind *p_bind);

public:
	template <class T>
	static void _add_class() {
		_add_class2(T::get_class_static(), T::get_parent_class_static());
	}

	template <class T>
	static void register_class() {
		T::initialize_class();
		ClassInfo *info = get_class_info(T::get_class_static());
		info->creator = &_creator<T>;
	}

	template <class T>
	static void register_abstract_class() {
		T::initialize_class();
	}

	template <class M, class... Defaults>
	static MethodBind *bind_method(const char *p_name, M p_method, Defaults... p_defaults) {
		MethodBind *bind = create_method_bind(p_method);
		bind->name = p_name;
		bind->default_arguments = std::vector<Variant>{ Variant(p_defaults)... };
		return _bind_methodp(bind);
	}

	template <class T>
	static MethodBind *bind_vararg_method(const char *p_name, Variant (T::*p_method)(const Variant **, int, CallError &)) {
		MethodBind *bind = new MethodBindVarArg<T>(p_method);
		bind->name = p_name;
		return _bind_methodp(bind);
	}

	static void add_property(const String &p_class, const PropertyInfo &p_info, const String &p_setter, const String &p_getter);
	static void add_property_group(const String &p_class, const String &p_name);
	static void add_signal(const String &p_class, const MethodInfo &p_signal);
	static void bind_integer_constant(const String &p_class, const String &p_name, int64_t p_value);

	static ClassInfo *get_class_info(const String &p_class);
	static bool class_exists(const String &p_class);
	static bool can_instantiate(const String &p_class);
	static Object *instantiate(const String &p_class);
	static bool is_parent_class(const String &p_class, const String &p_inherits);
	static String get_parent_class(const String &p_class);
	static void get_class_list(std::vector<String> *p_list);
	static void get_direct_inheriters(const String &p_class, std::vector<String> *p_list);

	static MethodBind *get_method(ClassInfo *p_info, const String &p_method);
	static bool has_signal(ClassInfo *p_info, const String &p_signal);
	static void get_signal_list(const String &p_class, std::vector<MethodInfo> *p_list);
	static bool get_integer_constant(const String &p_class, const String &p_name, int64_t &r_value);

	static void get_property_list(ClassInfo *p_info, std::vector<PropertyInfo> *p_list, bool p_no_inheritance = false);
	static bool set_property(Object *p_object, const String &p_name, const Variant &p_value);
	static bool get_property(const Object *p_object, const String &p_name, Variant &r_value);
	static const PropertySetGet *find_property(ClassInfo *p_info, const String &p_name);
	static Variant class_get_default_property_value(const String &p_class, const String &p_property, bool *r_valid = nullptr);

	static void cleanup();
};

#define ADD_PROPERTY(m_info, m_setter, m_getter) ClassDB::add_property(get_class_static(), m_info, m_setter, m_getter)
#define ADD_GROUP(m_name) ClassDB::add_property_group(get_class_static(), m_name)
#define ADD_SIGNAL(m_signal) ClassDB::add_signal(get_class_static(), m_signal)
#define BIND_CONSTANT(m_constant) ClassDB::bind_integer_constant(get_class_static(), #m_constant, m_constant)
#define BIND_ENUM_CONSTANT(m_constant) ClassDB::bind_integer_constant(get_class_static(), #m_constant, m_constant)
#define GDREGISTER_CLASS(m_class) ClassDB::register_class<m_class>()
#define GDREGISTER_ABSTRACT_CLASS(m_class) ClassDB::register_abstract_class<m_class>()
