#include "variant.h"

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

// ---------------------------------------------------------------------------
// Array
// ---------------------------------------------------------------------------
Array::Array() :
		p(std::make_shared<std::vector<Variant>>()) {}
int Array::size() const { return (int)p->size(); }
bool Array::is_empty() const { return p->empty(); }
void Array::clear() { p->clear(); }
Variant &Array::operator[](int p_idx) { return (*p)[p_idx]; }
const Variant &Array::operator[](int p_idx) const { return (*p)[p_idx]; }
void Array::push_back(const Variant &p_value) { p->push_back(p_value); }
void Array::insert(int p_pos, const Variant &p_value) {
	p_pos = Math::clamp(p_pos, 0, (int)p->size());
	p->insert(p->begin() + p_pos, p_value);
}
void Array::remove_at(int p_idx) {
	if (p_idx >= 0 && p_idx < (int)p->size()) {
		p->erase(p->begin() + p_idx);
	}
}
void Array::erase(const Variant &p_value) {
	int idx = find(p_value);
	if (idx >= 0) {
		remove_at(idx);
	}
}
int Array::find(const Variant &p_value) const {
	for (size_t i = 0; i < p->size(); i++) {
		if ((*p)[i] == p_value) {
			return (int)i;
		}
	}
	return -1;
}
bool Array::has(const Variant &p_value) const { return find(p_value) >= 0; }
Variant Array::pop_back() {
	if (p->empty()) {
		return Variant();
	}
	Variant v = p->back();
	p->pop_back();
	return v;
}
Variant Array::pop_front() {
	if (p->empty()) {
		return Variant();
	}
	Variant v = p->front();
	p->erase(p->begin());
	return v;
}
void Array::resize(int p_size) { p->resize(std::max(0, p_size)); }
Array Array::duplicate() const {
	Array a;
	*a.p = *p;
	return a;
}
std::vector<Variant> &Array::vec() { return *p; }
const std::vector<Variant> &Array::vec() const { return *p; }

// ---------------------------------------------------------------------------
// Dictionary
// ---------------------------------------------------------------------------
Dictionary::Dictionary() :
		p(std::make_shared<std::map<Variant, Variant>>()) {}
int Dictionary::size() const { return (int)p->size(); }
bool Dictionary::is_empty() const { return p->empty(); }
void Dictionary::clear() { p->clear(); }
bool Dictionary::has(const Variant &p_key) const { return p->find(p_key) != p->end(); }
Variant &Dictionary::operator[](const Variant &p_key) { return (*p)[p_key]; }
Variant Dictionary::get(const Variant &p_key, const Variant &p_default) const {
	auto it = p->find(p_key);
	return it == p->end() ? p_default : it->second;
}
void Dictionary::erase(const Variant &p_key) { p->erase(p_key); }
Array Dictionary::keys() const {
	Array a;
	for (auto &kv : *p) {
		a.push_back(kv.first);
	}
	return a;
}
Array Dictionary::values() const {
	Array a;
	for (auto &kv : *p) {
		a.push_back(kv.second);
	}
	return a;
}
Dictionary Dictionary::duplicate() const {
	Dictionary d;
	*d.p = *p;
	return d;
}
std::map<Variant, Variant> &Dictionary::map() { return *p; }
const std::map<Variant, Variant> &Dictionary::map() const { return *p; }

// ---------------------------------------------------------------------------
// Callable / Signal
// ---------------------------------------------------------------------------
Callable::Callable(const Object *p_object, const String &p_method) {
	object_id = p_object ? p_object->get_instance_id() : 0;
	method = p_method;
}

Callable Callable::from_function(CustomFunc p_func) {
	Callable c;
	c.custom = std::make_shared<CustomFunc>(std::move(p_func));
	return c;
}

bool Callable::is_valid() const {
	if (custom) {
		return true;
	}
	return ObjectDB::get_instance(object_id) != nullptr;
}

Object *Callable::get_object() const {
	return ObjectDB::get_instance(object_id);
}

Variant Callable::callp(const Variant **p_args, int p_argc, CallError &r_error) const {
	r_error.error = CallError::CALL_OK;
	if (custom) {
		return (*custom)(p_args, p_argc);
	}
	Object *obj = ObjectDB::get_instance(object_id);
	if (!obj) {
		r_error.error = CallError::CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}
	return obj->callp(method, p_args, p_argc, r_error);
}

Signal::Signal(const Object *p_object, const String &p_name) {
	object_id = p_object ? p_object->get_instance_id() : 0;
	name = p_name;
}

Object *Signal::get_object() const {
	return ObjectDB::get_instance(object_id);
}

// ---------------------------------------------------------------------------
// VariantObjRef
// ---------------------------------------------------------------------------
VariantObjRef::VariantObjRef(const Object *p_obj) {
	ptr = const_cast<Object *>(p_obj);
	if (ptr) {
		id = ptr->get_instance_id();
		if (ptr->is_ref_counted()) {
			static_cast<RefCounted *>(ptr)->reference();
			counted = true;
		}
	}
}

VariantObjRef::VariantObjRef(const VariantObjRef &p_other) {
	id = p_other.id;
	ptr = p_other.ptr;
	counted = p_other.counted;
	if (counted) {
		static_cast<RefCounted *>(ptr)->reference();
	}
}

VariantObjRef &VariantObjRef::operator=(const VariantObjRef &p_other) {
	if (this == &p_other) {
		return *this;
	}
	if (p_other.counted) {
		static_cast<RefCounted *>(p_other.ptr)->reference();
	}
	Object *old_ptr = ptr;
	bool old_counted = counted;
	id = p_other.id;
	ptr = p_other.ptr;
	counted = p_other.counted;
	if (old_counted && static_cast<RefCounted *>(old_ptr)->unreference()) {
		memdelete(old_ptr);
	}
	return *this;
}

VariantObjRef::~VariantObjRef() {
	if (counted && static_cast<RefCounted *>(ptr)->unreference()) {
		memdelete(ptr);
	}
}

Object *VariantObjRef::get() const {
	if (!ptr) {
		return nullptr;
	}
	if (counted) {
		return ptr;
	}
	return ObjectDB::get_instance(id); // nullptr si l'objet a été libéré
}

// ---------------------------------------------------------------------------
// Variant
// ---------------------------------------------------------------------------
static const char *_type_names[Variant::VARIANT_MAX] = {
	"Nil", "bool", "int", "float", "String", "Vector2", "Vector3", "Rect2", "Color", "Object", "Array", "Dictionary", "Callable", "Signal"
};

String Variant::get_type_name(Type p_type) {
	if (p_type < 0 || p_type >= VARIANT_MAX) {
		return "?";
	}
	return _type_names[p_type];
}

Variant::Type Variant::get_type_by_name(const String &p_name) {
	for (int i = 0; i < VARIANT_MAX; i++) {
		if (p_name == _type_names[i]) {
			return (Type)i;
		}
	}
	return VARIANT_MAX;
}

Variant Variant::construct_default(Type p_type) {
	switch (p_type) {
		case BOOL: return Variant(false);
		case INT: return Variant(0);
		case FLOAT: return Variant(0.0);
		case STRING: return Variant(String());
		case VECTOR2: return Variant(Vector2());
		case VECTOR3: return Variant(Vector3());
		case RECT2: return Variant(Rect2());
		case COLOR: return Variant(Color(0, 0, 0, 1));
		case OBJECT: return Variant((const Object *)nullptr);
		case ARRAY: return Variant(Array());
		case DICTIONARY: return Variant(Dictionary());
		case CALLABLE: return Variant(Callable());
		case SIGNAL: return Variant(Signal());
		default: return Variant();
	}
}

bool Variant::is_null() const {
	if (get_type() == NIL) {
		return true;
	}
	if (get_type() == OBJECT) {
		return std::get<VariantObjRef>(data).get() == nullptr;
	}
	return false;
}

bool Variant::booleanize() const {
	switch (get_type()) {
		case NIL: return false;
		case BOOL: return std::get<bool>(data);
		case INT: return std::get<int64_t>(data) != 0;
		case FLOAT: return std::get<double>(data) != 0.0;
		case STRING: return !std::get<String>(data).empty();
		case VECTOR2: return std::get<Vector2>(data) != Vector2();
		case VECTOR3: return std::get<Vector3>(data) != Vector3();
		case RECT2: return std::get<Rect2>(data) != Rect2();
		case COLOR: return true;
		case OBJECT: return std::get<VariantObjRef>(data).get() != nullptr;
		case ARRAY: return !std::get<Array>(data).is_empty();
		case DICTIONARY: return !std::get<Dictionary>(data).is_empty();
		case CALLABLE: return !std::get<Callable>(data).is_null();
		case SIGNAL: return std::get<Signal>(data).object_id != 0;
		default: return false;
	}
}

int64_t Variant::as_int() const {
	switch (get_type()) {
		case BOOL: return std::get<bool>(data) ? 1 : 0;
		case INT: return std::get<int64_t>(data);
		case FLOAT: return (int64_t)std::get<double>(data);
		case STRING: return strtoll(std::get<String>(data).c_str(), nullptr, 10);
		default: return 0;
	}
}

double Variant::as_float() const {
	switch (get_type()) {
		case BOOL: return std::get<bool>(data) ? 1.0 : 0.0;
		case INT: return (double)std::get<int64_t>(data);
		case FLOAT: return std::get<double>(data);
		case STRING: return strtod(std::get<String>(data).c_str(), nullptr);
		default: return 0.0;
	}
}

String Variant::as_string() const {
	if (get_type() == STRING) {
		return std::get<String>(data);
	}
	if (get_type() == NIL) {
		return String();
	}
	return stringify();
}

Vector2 Variant::as_vector2() const {
	return get_type() == VECTOR2 ? std::get<Vector2>(data) : Vector2();
}

Vector3 Variant::as_vector3() const {
	return get_type() == VECTOR3 ? std::get<Vector3>(data) : Vector3();
}

Rect2 Variant::as_rect2() const {
	return get_type() == RECT2 ? std::get<Rect2>(data) : Rect2();
}

Color Variant::as_color() const {
	if (get_type() == COLOR) {
		return std::get<Color>(data);
	}
	if (get_type() == STRING) {
		const String &s = std::get<String>(data);
		Color c;
		if (Color::find_named(str_to_upper(s), c)) {
			return c;
		}
		return Color::html(s);
	}
	return Color(0, 0, 0, 1);
}

Object *Variant::as_object() const {
	return get_type() == OBJECT ? std::get<VariantObjRef>(data).get() : nullptr;
}

Array Variant::as_array() const {
	return get_type() == ARRAY ? std::get<Array>(data) : Array();
}

Dictionary Variant::as_dictionary() const {
	return get_type() == DICTIONARY ? std::get<Dictionary>(data) : Dictionary();
}

Callable Variant::as_callable() const {
	return get_type() == CALLABLE ? std::get<Callable>(data) : Callable();
}

Signal Variant::as_signal() const {
	return get_type() == SIGNAL ? std::get<Signal>(data) : Signal();
}

Variant Variant::convert_to(Type p_type) const {
	if (p_type == get_type() || p_type == NIL) {
		return *this;
	}
	switch (p_type) {
		case BOOL: return Variant(as_bool());
		case INT:
			if (is_num() || get_type() == BOOL || get_type() == STRING) {
				return Variant(as_int());
			}
			break;
		case FLOAT:
			if (is_num() || get_type() == BOOL || get_type() == STRING) {
				return Variant(as_float());
			}
			break;
		case STRING: return Variant(as_string());
		case COLOR:
			if (get_type() == STRING) {
				return Variant(as_color());
			}
			break;
		case OBJECT:
			if (get_type() == NIL) {
				return Variant((const Object *)nullptr);
			}
			break;
		default:
			break;
	}
	return *this;
}

static String _float_str(double v) {
	String s = rtos(v);
	if (s.find_first_of(".einf") == String::npos) {
		s += ".0";
	}
	return s;
}

static String _stringify_inner(const Variant &v) {
	if (v.get_type() == Variant::STRING) {
		return "\"" + str_c_escape(v.as_string()) + "\"";
	}
	return v.stringify();
}

String Variant::stringify() const {
	switch (get_type()) {
		case NIL: return "<null>";
		case BOOL: return std::get<bool>(data) ? "true" : "false";
		case INT: return itos(std::get<int64_t>(data));
		case FLOAT: return _float_str(std::get<double>(data));
		case STRING: return std::get<String>(data);
		case VECTOR2: {
			const Vector2 &v = std::get<Vector2>(data);
			return "(" + _float_str(v.x) + ", " + _float_str(v.y) + ")";
		}
		case VECTOR3: {
			const Vector3 &v = std::get<Vector3>(data);
			return "(" + _float_str(v.x) + ", " + _float_str(v.y) + ", " + _float_str(v.z) + ")";
		}
		case RECT2: {
			const Rect2 &r = std::get<Rect2>(data);
			return "[P: (" + _float_str(r.position.x) + ", " + _float_str(r.position.y) + "), S: (" + _float_str(r.size.x) + ", " + _float_str(r.size.y) + ")]";
		}
		case COLOR: {
			const Color &c = std::get<Color>(data);
			return "(" + rtos(c.r) + ", " + rtos(c.g) + ", " + rtos(c.b) + ", " + rtos(c.a) + ")";
		}
		case OBJECT: {
			const VariantObjRef &ref = std::get<VariantObjRef>(data);
			Object *obj = ref.get();
			if (!obj) {
				return ref.ptr ? "<Objet libéré>" : "<null>";
			}
			return obj->to_string();
		}
		case ARRAY: {
			const Array &a = std::get<Array>(data);
			String s = "[";
			for (int i = 0; i < a.size(); i++) {
				if (i) {
					s += ", ";
				}
				s += _stringify_inner(a[i]);
			}
			return s + "]";
		}
		case DICTIONARY: {
			const Dictionary &d = std::get<Dictionary>(data);
			String s = "{ ";
			bool first = true;
			for (auto &kv : d.map()) {
				if (!first) {
					s += ", ";
				}
				first = false;
				s += _stringify_inner(kv.first) + ": " + _stringify_inner(kv.second);
			}
			return s + " }";
		}
		case CALLABLE: {
			const Callable &c = std::get<Callable>(data);
			if (c.custom) {
				return "<Callable C++>";
			}
			Object *obj = c.get_object();
			return (obj ? obj->get_class() : String("null")) + "::" + c.method;
		}
		case SIGNAL: {
			const Signal &s = std::get<Signal>(data);
			Object *obj = s.get_object();
			return (obj ? obj->get_class() : String("null")) + "::[signal]" + s.name;
		}
		default: return "";
	}
}

bool Variant::operator==(const Variant &p_other) const {
	Type ta = get_type(), tb = p_other.get_type();
	if (ta != tb) {
		if (is_num() && p_other.is_num()) {
			return as_float() == p_other.as_float();
		}
		// `objet == null` est vrai pour un objet nul ou libéré.
		if ((ta == NIL && tb == OBJECT) || (ta == OBJECT && tb == NIL)) {
			return is_null() && p_other.is_null();
		}
		return false;
	}
	switch (ta) {
		case NIL: return true;
		case BOOL: return std::get<bool>(data) == std::get<bool>(p_other.data);
		case INT: return std::get<int64_t>(data) == std::get<int64_t>(p_other.data);
		case FLOAT: return std::get<double>(data) == std::get<double>(p_other.data);
		case STRING: return std::get<String>(data) == std::get<String>(p_other.data);
		case VECTOR2: return std::get<Vector2>(data) == std::get<Vector2>(p_other.data);
		case VECTOR3: return std::get<Vector3>(data) == std::get<Vector3>(p_other.data);
		case RECT2: return std::get<Rect2>(data) == std::get<Rect2>(p_other.data);
		case COLOR: return std::get<Color>(data) == std::get<Color>(p_other.data);
		case OBJECT: return std::get<VariantObjRef>(data).get() == std::get<VariantObjRef>(p_other.data).get();
		case ARRAY: {
			const Array &a = std::get<Array>(data);
			const Array &b = std::get<Array>(p_other.data);
			if (a.id() == b.id()) {
				return true;
			}
			if (a.size() != b.size()) {
				return false;
			}
			for (int i = 0; i < a.size(); i++) {
				if (a[i] != b[i]) {
					return false;
				}
			}
			return true;
		}
		case DICTIONARY: {
			const Dictionary &a = std::get<Dictionary>(data);
			const Dictionary &b = std::get<Dictionary>(p_other.data);
			if (a.id() == b.id()) {
				return true;
			}
			if (a.size() != b.size()) {
				return false;
			}
			auto ia = a.map().begin();
			auto ib = b.map().begin();
			for (; ia != a.map().end(); ++ia, ++ib) {
				if (ia->first != ib->first || ia->second != ib->second) {
					return false;
				}
			}
			return true;
		}
		case CALLABLE: return std::get<Callable>(data) == std::get<Callable>(p_other.data);
		case SIGNAL: return std::get<Signal>(data) == std::get<Signal>(p_other.data);
		default: return false;
	}
}

bool Variant::operator<(const Variant &p_other) const {
	Type ta = get_type(), tb = p_other.get_type();
	if (is_num() && p_other.is_num()) {
		return as_float() < p_other.as_float();
	}
	if (ta != tb) {
		return ta < tb;
	}
	switch (ta) {
		case BOOL: return std::get<bool>(data) < std::get<bool>(p_other.data);
		case STRING: return std::get<String>(data) < std::get<String>(p_other.data);
		case VECTOR2: return std::get<Vector2>(data) < std::get<Vector2>(p_other.data);
		case VECTOR3: return std::get<Vector3>(data) < std::get<Vector3>(p_other.data);
		case OBJECT: return std::get<VariantObjRef>(data).id < std::get<VariantObjRef>(p_other.data).id;
		case ARRAY: return std::get<Array>(data).id() < std::get<Array>(p_other.data).id();
		case DICTIONARY: return std::get<Dictionary>(data).id() < std::get<Dictionary>(p_other.data).id();
		case COLOR: return std::get<Color>(data).to_rgba32() < std::get<Color>(p_other.data).to_rgba32();
		default: return false;
	}
}
