#include "object.h"

#include "core/object/ref_counted.h"
#include "core/object/script_language.h"
#include "core/string/ustring.h"

std::unordered_map<ObjectID, Object *> ObjectDB::instances;
ObjectID ObjectDB::next_id = 1;

ObjectID ObjectDB::add_instance(Object *p_object) {
	ObjectID id = next_id++;
	instances[id] = p_object;
	return id;
}

void ObjectDB::remove_instance(Object *p_object) {
	instances.erase(p_object->get_instance_id());
}

void memdelete(Object *p_object) {
	if (!p_object) {
		return;
	}
	p_object->notification(Object::NOTIFICATION_PREDELETE, true);
	delete p_object;
}

// ---------------------------------------------------------------------------

Object::Object() {
	_instance_id = ObjectDB::add_instance(this);
}

Object::~Object() {
	if (script_instance) {
		delete script_instance;
		script_instance = nullptr;
	}
	ObjectDB::remove_instance(this);
	_instance_id = 0;
}

void Object::initialize_class() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;
	ClassDB::_add_class<Object>();
	_bind_methods();
}

ClassInfo *Object::get_class_info() const {
	if (!_class_info) {
		_class_info = ClassDB::get_class_info(get_class());
	}
	return _class_info;
}

String Object::to_string() const {
	return "<" + get_class() + "#" + itos((int64_t)_instance_id) + ">";
}

void Object::set(const String &p_name, const Variant &p_value, bool *r_valid) {
	if (r_valid) {
		*r_valid = true;
	}
	if (ClassDB::set_property(this, p_name, p_value)) {
		return;
	}
	if (script_instance && script_instance->set(p_name, p_value)) {
		return;
	}
	if (r_valid) {
		*r_valid = false;
	}
}

Variant Object::get(const String &p_name, bool *r_valid) const {
	if (r_valid) {
		*r_valid = true;
	}
	Variant ret;
	if (ClassDB::get_property(this, p_name, ret)) {
		return ret;
	}
	if (script_instance && script_instance->get(p_name, ret)) {
		return ret;
	}
	if (r_valid) {
		*r_valid = false;
	}
	return Variant();
}

void Object::get_property_list(std::vector<PropertyInfo> *p_list) const {
	ClassDB::get_property_list(get_class_info(), p_list);
	if (script_instance) {
		script_instance->get_property_list(p_list);
	}
}

bool Object::has_method(const String &p_method) const {
	if (script_instance && script_instance->has_method(p_method)) {
		return true;
	}
	return ClassDB::get_method(get_class_info(), p_method) != nullptr;
}

Variant Object::callp(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error) {
	r_error.error = CallError::CALL_OK;
	if (script_instance) {
		Variant ret = script_instance->callp(p_method, p_args, p_argc, r_error);
		if (r_error.error != CallError::CALL_ERROR_INVALID_METHOD) {
			return ret;
		}
		r_error.error = CallError::CALL_OK;
	}
	MethodBind *bind = ClassDB::get_method(get_class_info(), p_method);
	if (bind) {
		return bind->call(this, p_args, p_argc, r_error);
	}
	r_error.error = CallError::CALL_ERROR_INVALID_METHOD;
	return Variant();
}

void Object::notification(int p_what, bool p_reversed) {
	_notificationv(p_what, p_reversed);
}

// ---------------------------------------------------------------------------
// Signaux
// ---------------------------------------------------------------------------
void Object::add_user_signal(const String &p_name) {
	user_signals.insert(p_name);
}

bool Object::has_signal(const String &p_name) const {
	if (user_signals.count(p_name)) {
		return true;
	}
	if (ClassDB::has_signal(get_class_info(), p_name)) {
		return true;
	}
	if (script_instance) {
		Ref<Script> s = script_instance->get_script();
		if (s.is_valid() && s->has_script_signal(p_name)) {
			return true;
		}
	}
	return false;
}

Error Object::connect(const String &p_signal, const Callable &p_callable, uint32_t p_flags) {
	ERR_FAIL_COND_V_MSG(p_callable.is_null(), ERR_INVALID_PARAMETER, "connect(): Callable nul pour le signal '" + p_signal + "'.");
	ERR_FAIL_COND_V_MSG(!has_signal(p_signal), ERR_DOES_NOT_EXIST, "connect(): le signal '" + p_signal + "' n'existe pas dans " + get_class() + ".");
	std::vector<Connection> &conns = signal_map[p_signal];
	for (const Connection &c : conns) {
		if (c.callable == p_callable) {
			return ERR_ALREADY_EXISTS; // déjà connecté : on ignore silencieusement
		}
	}
	Connection c;
	c.callable = p_callable;
	c.flags = p_flags;
	conns.push_back(c);
	return OK;
}

void Object::disconnect(const String &p_signal, const Callable &p_callable) {
	auto it = signal_map.find(p_signal);
	if (it == signal_map.end()) {
		return;
	}
	std::vector<Connection> &conns = it->second;
	for (size_t i = 0; i < conns.size(); i++) {
		if (conns[i].callable == p_callable) {
			conns.erase(conns.begin() + i);
			return;
		}
	}
}

bool Object::is_connected(const String &p_signal, const Callable &p_callable) const {
	auto it = signal_map.find(p_signal);
	if (it == signal_map.end()) {
		return false;
	}
	for (const Connection &c : it->second) {
		if (c.callable == p_callable) {
			return true;
		}
	}
	return false;
}

void Object::get_signal_connection_list(const String &p_signal, std::vector<Connection> *p_list) const {
	auto it = signal_map.find(p_signal);
	if (it != signal_map.end()) {
		*p_list = it->second;
	}
}

void Object::emit_signalp(const String &p_name, const Variant **p_args, int p_argc) {
	auto it = signal_map.find(p_name);
	if (it == signal_map.end() || it->second.empty()) {
		return;
	}
	const ObjectID self_id = _instance_id;
	const String class_name = get_class();
	std::vector<Connection> conns = it->second; // copie : les handlers peuvent (dé)connecter
	for (const Connection &c : conns) {
		if (!c.callable.is_valid()) {
			disconnect(p_name, c.callable); // cible libérée
			continue;
		}
		if (c.flags & CONNECT_ONE_SHOT) {
			disconnect(p_name, c.callable);
		}
		CallError ce;
		c.callable.callp(p_args, p_argc, ce);
		if (ce.error == CallError::CALL_ERROR_INVALID_METHOD) {
			print_error("Signal '" + p_name + "' de " + class_name + " : méthode cible '" + c.callable.method + "' introuvable.");
		} else if (ce.error != CallError::CALL_OK) {
			print_error("Signal '" + p_name + "' de " + class_name + " : mauvais nombre d'arguments pour '" + c.callable.method + "'.");
		}
		if (!ObjectDB::get_instance(self_id)) {
			return; // l'émetteur a été détruit par un handler
		}
	}
}

// ---------------------------------------------------------------------------
// Script et métadonnées
// ---------------------------------------------------------------------------
void Object::set_script(const Variant &p_script) {
	if (script == p_script && (script_instance || p_script.is_null())) {
		return;
	}
	if (script_instance) {
		delete script_instance;
		script_instance = nullptr;
	}
	script = Variant();
	Ref<Script> s = Ref<Script>(Object::cast_to<Script>(p_script.as_object()));
	if (s.is_valid()) {
		script = Variant(s);
		if (ScriptServer::is_scripting_enabled()) {
			script_instance = s->instance_create(this);
		} else {
			script_instance = s->placeholder_instance_create(this);
		}
	}
	_script_changed();
}

void Object::set_meta(const String &p_name, const Variant &p_value) {
	if (p_value.get_type() == Variant::NIL) {
		metadata.erase(p_name);
	} else {
		metadata[p_name] = p_value;
	}
}

Variant Object::get_meta(const String &p_name, const Variant &p_default) const {
	auto it = metadata.find(p_name);
	return it == metadata.end() ? p_default : it->second;
}

bool Object::has_meta(const String &p_name) const {
	return metadata.count(p_name) > 0;
}

// ---------------------------------------------------------------------------
// Liaisons
// ---------------------------------------------------------------------------
Variant Object::_emit_signal_bind(const Variant **p_args, int p_argc, CallError &r_error) {
	if (p_argc < 1) {
		r_error.error = CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 1;
		return Variant();
	}
	String signal = p_args[0]->as_string();
	if (!has_signal(signal)) {
		print_error("emit_signal(): le signal '" + signal + "' n'existe pas dans " + get_class() + ".");
		return Variant();
	}
	emit_signalp(signal, p_args + 1, p_argc - 1);
	return Variant();
}

Variant Object::_call_bind(const Variant **p_args, int p_argc, CallError &r_error) {
	if (p_argc < 1) {
		r_error.error = CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 1;
		return Variant();
	}
	return callp(p_args[0]->as_string(), p_args + 1, p_argc - 1, r_error);
}

Error Object::_connect_bind(const String &p_signal, const Callable &p_callable, int p_flags) {
	return connect(p_signal, p_callable, (uint32_t)p_flags);
}

void Object::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_class"), &Object::get_class);
	ClassDB::bind_method(D_METHOD("is_class", "class"), &Object::is_class);
	ClassDB::bind_method(D_METHOD("get_instance_id"), &Object::get_instance_id);
	ClassDB::bind_method(D_METHOD("set_script", "script"), &Object::set_script);
	ClassDB::bind_method(D_METHOD("get_script"), &Object::get_script);
	ClassDB::bind_method(D_METHOD("has_method", "method"), &Object::has_method);
	ClassDB::bind_method(D_METHOD("has_signal", "signal"), &Object::has_signal);
	ClassDB::bind_method(D_METHOD("add_user_signal", "signal"), &Object::add_user_signal);
	ClassDB::bind_method(D_METHOD("connect", "signal", "callable", "flags"), &Object::_connect_bind, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("disconnect", "signal", "callable"), &Object::disconnect);
	ClassDB::bind_method(D_METHOD("is_connected", "signal", "callable"), &Object::is_connected);
	ClassDB::bind_method(D_METHOD("set_meta", "name", "value"), &Object::set_meta);
	ClassDB::bind_method(D_METHOD("get_meta", "name", "default"), &Object::get_meta, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("has_meta", "name"), &Object::has_meta);
	ClassDB::bind_method(D_METHOD("to_string"), &Object::to_string);
	ClassDB::bind_vararg_method("emit_signal", &Object::_emit_signal_bind);
	ClassDB::bind_vararg_method("call", &Object::_call_bind);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "script", PROPERTY_HINT_RESOURCE_TYPE, "Script"), "set_script", "get_script");

	BIND_CONSTANT(CONNECT_ONE_SHOT);
}

void RefCounted::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_reference_count"), &RefCounted::get_reference_count);
}
