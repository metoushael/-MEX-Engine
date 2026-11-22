// Variant : conteneur de valeur dynamique, au cœur de la réflexion et du scripting.
#pragma once

#include "core/math/color.h"
#include "core/math/rect2.h"
#include "core/math/transform_2d.h"
#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/typedefs.h"

#include <type_traits>
#include <variant>

class Object;
class Variant;
template <class T>
class Ref;

struct CallError {
	enum Type {
		CALL_OK,
		CALL_ERROR_INVALID_METHOD,
		CALL_ERROR_INVALID_ARGUMENT,
		CALL_ERROR_TOO_MANY_ARGUMENTS,
		CALL_ERROR_TOO_FEW_ARGUMENTS,
		CALL_ERROR_INSTANCE_IS_NULL,
	};
	Type error = CALL_OK;
	int expected = 0;
};

// Tableau dynamique partagé par référence (comme dans Godot).
class Array {
	std::shared_ptr<std::vector<Variant>> p;

public:
	Array();
	int size() const;
	bool is_empty() const;
	void clear();
	Variant &operator[](int p_idx);
	const Variant &operator[](int p_idx) const;
	void push_back(const Variant &p_value);
	void insert(int p_pos, const Variant &p_value);
	void remove_at(int p_idx);
	void erase(const Variant &p_value);
	int find(const Variant &p_value) const;
	bool has(const Variant &p_value) const;
	Variant pop_back();
	Variant pop_front();
	void resize(int p_size);
	Array duplicate() const;
	std::vector<Variant> &vec();
	const std::vector<Variant> &vec() const;
	const void *id() const { return p.get(); }
};

// Dictionnaire partagé par référence (clés triées).
class Dictionary {
	std::shared_ptr<std::map<Variant, Variant>> p;

public:
	Dictionary();
	int size() const;
	bool is_empty() const;
	void clear();
	bool has(const Variant &p_key) const;
	Variant &operator[](const Variant &p_key);
	Variant get(const Variant &p_key, const Variant &p_default) const;
	void erase(const Variant &p_key);
	Array keys() const;
	Array values() const;
	Dictionary duplicate() const;
	std::map<Variant, Variant> &map();
	const std::map<Variant, Variant> &map() const;
	const void *id() const { return p.get(); }
};

// Référence vers une méthode d'un objet (ou une fonction C++ arbitraire).
class Callable {
public:
	typedef std::function<Variant(const Variant **, int)> CustomFunc;

	ObjectID object_id = 0;
	String method;
	std::shared_ptr<CustomFunc> custom;

	Callable() {}
	Callable(const Object *p_object, const String &p_method);
	static Callable from_function(CustomFunc p_func);

	bool is_null() const { return object_id == 0 && !custom; }
	bool is_valid() const;
	Object *get_object() const;
	Variant callp(const Variant **p_args, int p_argc, CallError &r_error) const;
	bool operator==(const Callable &c) const { return object_id == c.object_id && method == c.method && custom == c.custom; }
};

// Référence vers un signal d'un objet (permet `objet.signal.connect(...)`).
class Signal {
public:
	ObjectID object_id = 0;
	String name;

	Signal() {}
	Signal(const Object *p_object, const String &p_name);
	Object *get_object() const;
	bool operator==(const Signal &s) const { return object_id == s.object_id && name == s.name; }
};

// Référence d'objet stockée dans un Variant. Garde l'ID pour détecter les objets
// libérés et retient une référence sur les RefCounted.
struct VariantObjRef {
	ObjectID id = 0;
	Object *ptr = nullptr;
	bool counted = false;

	VariantObjRef() {}
	VariantObjRef(const Object *p_obj);
	VariantObjRef(const VariantObjRef &p_other);
	VariantObjRef &operator=(const VariantObjRef &p_other);
	~VariantObjRef();
	Object *get() const;
};

class Variant {
public:
	enum Type {
		NIL,
		BOOL,
		INT,
		FLOAT,
		STRING,
		VECTOR2,
		VECTOR3,
		RECT2,
		COLOR,
		OBJECT,
		ARRAY,
		DICTIONARY,
		CALLABLE,
		SIGNAL,
		VARIANT_MAX,
	};

	enum Operator {
		OP_ADD,
		OP_SUB,
		OP_MUL,
		OP_DIV,
		OP_MOD,
		OP_NEG,
		OP_EQ,
		OP_NE,
		OP_LT,
		OP_LE,
		OP_GT,
		OP_GE,
		OP_IN,
	};

private:
	typedef std::variant<std::monostate, bool, int64_t, double, String, Vector2, Vector3, Rect2, Color, VariantObjRef, Array, Dictionary, Callable, Signal> Data;
	Data data;

public:
	Variant() {}
	Variant(std::nullptr_t) {}
	Variant(bool v) :
			data(std::in_place_type<bool>, v) {}
	template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
	Variant(T v) :
			data(std::in_place_type<int64_t>, (int64_t)v) {}
	template <class T, std::enable_if_t<std::is_enum_v<T>, long> = 0>
	Variant(T v) :
			data(std::in_place_type<int64_t>, (int64_t)v) {}
	Variant(float v) :
			data(std::in_place_type<double>, (double)v) {}
	Variant(double v) :
			data(std::in_place_type<double>, v) {}
	Variant(const char *v) :
			data(std::in_place_type<String>, v ? v : "") {}
	Variant(const String &v) :
			data(std::in_place_type<String>, v) {}
	Variant(const Vector2 &v) :
			data(std::in_place_type<Vector2>, v) {}
	Variant(const Vector3 &v) :
			data(std::in_place_type<Vector3>, v) {}
	Variant(const Rect2 &v) :
			data(std::in_place_type<Rect2>, v) {}
	Variant(const Color &v) :
			data(std::in_place_type<Color>, v) {}
	Variant(const Object *v) :
			data(std::in_place_type<VariantObjRef>, v) {}
	template <class T>
	Variant(const Ref<T> &v) :
			data(std::in_place_type<VariantObjRef>, (const Object *)v.ptr()) {}
	Variant(const Array &v) :
			data(std::in_place_type<Array>, v) {}
	Variant(const Dictionary &v) :
			data(std::in_place_type<Dictionary>, v) {}
	Variant(const Callable &v) :
			data(std::in_place_type<Callable>, v) {}
	Variant(const Signal &v) :
			data(std::in_place_type<Signal>, v) {}

	Type get_type() const { return (Type)data.index(); }
	static String get_type_name(Type p_type);
	static Type get_type_by_name(const String &p_name); // VARIANT_MAX si inconnu
	static Variant construct_default(Type p_type);

	bool is_null() const; // NIL ou objet nul/libéré
	bool is_num() const { return get_type() == INT || get_type() == FLOAT; }
	bool booleanize() const;

	bool as_bool() const { return booleanize(); }
	int64_t as_int() const;
	double as_float() const;
	String as_string() const;
	Vector2 as_vector2() const;
	Vector3 as_vector3() const;
	Rect2 as_rect2() const;
	Color as_color() const;
	Object *as_object() const;
	Array as_array() const;
	Dictionary as_dictionary() const;
	Callable as_callable() const;
	Signal as_signal() const;

	// Conversion (si possible) vers un autre type ; sinon retourne la valeur telle quelle.
	Variant convert_to(Type p_type) const;

	String stringify() const; // représentation lisible (print, str)

	bool operator==(const Variant &p_other) const;
	bool operator!=(const Variant &p_other) const { return !(*this == p_other); }
	bool operator<(const Variant &p_other) const;

	static void evaluate(Operator p_op, const Variant &p_a, const Variant &p_b, Variant &r_ret, bool &r_valid);
	static String get_operator_name(Operator p_op);

	Variant get_named(const String &p_name, bool &r_valid) const;
	void set_named(const String &p_name, const Variant &p_value, bool &r_valid);
	Variant get_indexed(const Variant &p_index, bool &r_valid) const;
	void set_indexed(const Variant &p_index, const Variant &p_value, bool &r_valid);

	// Appel d'une méthode intégrée (Vector2.length(), Array.append(), ...) ou d'objet.
	Variant call_method(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error);

	// Vrai pour les types "valeur" qu'il faut réécrire après modification d'un membre.
	bool is_value_type() const {
		Type t = get_type();
		return t == VECTOR2 || t == VECTOR3 || t == RECT2 || t == COLOR;
	}
};
