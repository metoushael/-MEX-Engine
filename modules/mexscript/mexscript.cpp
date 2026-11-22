// MexScript : interpréteur (parcours direct de l'arbre syntaxique).
#include "mexscript.h"

#include "core/config/engine.h"
#include "core/debug_monitor.h"
#include "core/os/os.h"
#include "core/io/file_access.h"
#include "core/string/ustring.h"

#include <cmath>
#include <set>
#include <unordered_map>

namespace {

struct MXError {
	String message;
	int line = 0;
	String function;
	String stack; // fonctions appelantes, pour le message d'erreur
};

int call_depth = 0;

// Pile des fonctions de script en cours (tous scripts confondus), pour les messages d'erreur,
// print_stack() et le profileur.
struct MXStackEntry {
	const MexScript *script = nullptr;
	const String *function = nullptr;
	const int *line = nullptr; // ligne en cours dans cette fonction
	uint64_t start_usec = 0;
	uint64_t children_usec = 0;
};
std::vector<MXStackEntry> mx_stack;
bool mx_profiling = false;
uint64_t mx_call_count = 0;
struct MXProfile {
	String source;
	String function;
	uint64_t calls = 0, total_usec = 0, self_usec = 0;
};
std::unordered_map<String, MXProfile> mx_profiles;

String mx_describe_callers() {
	// La fonction fautive est déjà nommée dans le message : seules les appelantes sont listées.
	String out;
	for (int i = (int)mx_stack.size() - 2; i >= 0; i--) {
		const MXStackEntry &e = mx_stack[(size_t)i];
		out += vformat("\n    appelée par %s (%s:%d)", e.function->c_str(), e.script->get_debug_path().c_str(), *e.line);
	}
	return out;
}
const int MAX_CALL_DEPTH = 200;

std::unordered_map<String, Ref<MexScriptNativeClass>> native_classes;

Variant _convert_typed(Variant::Type p_type, const Variant &p_value) {
	if (p_type == Variant::FLOAT && p_value.get_type() == Variant::INT) {
		return Variant((double)p_value.as_int());
	}
	if (p_type == Variant::INT && p_value.get_type() == Variant::FLOAT) {
		return Variant((int64_t)p_value.as_float());
	}
	if (p_type == Variant::BOOL && p_value.get_type() != Variant::BOOL && p_value.get_type() != Variant::NIL) {
		return Variant(p_value.booleanize());
	}
	return p_value;
}

Variant::Type _type_from_hint(const String &p_hint) {
	if (p_hint.empty()) {
		return Variant::NIL;
	}
	Variant::Type t = Variant::get_type_by_name(p_hint);
	if (t == Variant::VARIANT_MAX) {
		return Variant::OBJECT; // nom de classe
	}
	return t;
}

Variant _default_for_type(Variant::Type p_type) {
	if (p_type == Variant::NIL || p_type == Variant::OBJECT) {
		return Variant();
	}
	return Variant::construct_default(p_type);
}

bool _is_value_type(Variant::Type p_type) {
	return p_type == Variant::VECTOR2 || p_type == Variant::VECTOR3 || p_type == Variant::RECT2 || p_type == Variant::COLOR;
}

String _describe(const Variant &p_value) {
	if (p_value.get_type() == Variant::OBJECT) {
		Object *obj = p_value.as_object();
		return obj ? obj->get_class() : String("null");
	}
	return Variant::get_type_name(p_value.get_type());
}

} // namespace

// ---------------------------------------------------------------------------
// Interpréteur
// ---------------------------------------------------------------------------
struct MXInterpreter {
	enum Flow {
		FLOW_NORMAL,
		FLOW_BREAK,
		FLOW_CONTINUE,
		FLOW_RETURN,
	};

	struct Local {
		const String *name;
		Variant value;
	};

	struct Frame {
		std::vector<Local> locals;
		Variant ret;
		int line = 0;
		const MXFunction *function = nullptr;
	};

	typedef std::vector<std::pair<String, Variant>> Captures;

	MexScript *script = nullptr;
	MexScriptInstance *instance = nullptr;
	Object *owner = nullptr;
	Frame *frame = nullptr;

	MXInterpreter(MexScript *p_script, MexScriptInstance *p_instance, Object *p_owner) :
			script(p_script), instance(p_instance), owner(p_owner) {}

	[[noreturn]] void fail(const String &p_message) const {
		MXError err;
		err.message = p_message;
		if (frame) {
			err.line = frame->line;
			if (frame->function) {
				err.function = frame->function->name;
			}
		}
		err.stack = mx_describe_callers();
		throw err;
	}

	Variant *find_local(const String &p_name) {
		if (!frame) {
			return nullptr;
		}
		for (size_t i = frame->locals.size(); i > 0; i--) {
			Local &l = frame->locals[i - 1];
			if (*l.name == p_name) {
				return &l.value;
			}
		}
		return nullptr;
	}

	void check_call(const CallError &p_error, const String &p_method, const String &p_where) const {
		switch (p_error.error) {
			case CallError::CALL_OK:
				return;
			case CallError::CALL_ERROR_INVALID_METHOD:
				fail(vformat("Méthode '%s' introuvable dans %s.", p_method.c_str(), p_where.c_str()));
			case CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
				fail(vformat("Pas assez d'arguments pour '%s' (%d attendu(s)).", p_method.c_str(), p_error.expected));
			case CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
				fail(vformat("Trop d'arguments pour '%s' (%d attendu(s)).", p_method.c_str(), p_error.expected));
			case CallError::CALL_ERROR_INSTANCE_IS_NULL:
				fail(vformat("Appel de '%s' sur une instance nulle.", p_method.c_str()));
			default:
				fail(vformat("Argument invalide pour '%s'.", p_method.c_str()));
		}
	}

	// --- Types et constantes intégrés (Vector2.ZERO, Color.RED...) ---
	static bool is_builtin_static_type(const String &p_name) {
		return p_name == "Vector2" || p_name == "Vector3" || p_name == "Color";
	}

	static bool get_builtin_type_constant(const String &p_type, const String &p_name, Variant &r_value) {
		if (p_type == "Vector2") {
			static const std::unordered_map<String, Vector2> constants = {
				{ "ZERO", Vector2(0, 0) }, { "ONE", Vector2(1, 1) }, { "UP", Vector2(0, -1) },
				{ "DOWN", Vector2(0, 1) }, { "LEFT", Vector2(-1, 0) }, { "RIGHT", Vector2(1, 0) },
			};
			auto it = constants.find(p_name);
			if (it == constants.end()) {
				return false;
			}
			r_value = Variant(it->second);
			return true;
		}
		if (p_type == "Vector3") {
			static const std::unordered_map<String, Vector3> constants = {
				{ "ZERO", Vector3(0, 0, 0) }, { "ONE", Vector3(1, 1, 1) }, { "UP", Vector3(0, 1, 0) },
				{ "DOWN", Vector3(0, -1, 0) }, { "LEFT", Vector3(-1, 0, 0) }, { "RIGHT", Vector3(1, 0, 0) },
				{ "FORWARD", Vector3(0, 0, -1) }, { "BACK", Vector3(0, 0, 1) },
			};
			auto it = constants.find(p_name);
			if (it == constants.end()) {
				return false;
			}
			r_value = Variant(it->second);
			return true;
		}
		if (p_type == "Color") {
			Color c;
			if (!Color::find_named(p_name, c)) {
				return false;
			}
			r_value = Variant(c);
			return true;
		}
		return false;
	}

	Variant call_builtin_type_static(const String &p_type, const String &p_method, const Variant **p_args, int p_argc) {
		if (p_type == "Vector2" && p_method == "from_angle" && p_argc == 1) {
			const real_t a = (real_t)p_args[0]->as_float();
			return Variant(Vector2(std::cos(a), std::sin(a)));
		}
		if (p_type == "Color") {
			if (p_method == "from_hsv" && (p_argc == 3 || p_argc == 4)) {
				return Variant(Color::from_hsv((float)p_args[0]->as_float(), (float)p_args[1]->as_float(), (float)p_args[2]->as_float(), p_argc == 4 ? (float)p_args[3]->as_float() : 1.0f));
			}
			if (p_method == "html" && p_argc == 1) {
				return Variant(Color::html(p_args[0]->as_string()));
			}
			if (p_method == "from_rgba8" && (p_argc == 3 || p_argc == 4)) {
				return Variant(Color((float)p_args[0]->as_float() / 255.0f, (float)p_args[1]->as_float() / 255.0f, (float)p_args[2]->as_float() / 255.0f, p_argc == 4 ? (float)p_args[3]->as_float() / 255.0f : 1.0f));
			}
		}
		fail(vformat("Fonction statique '%s.%s' introuvable (ou nombre d'arguments incorrect).", p_type.c_str(), p_method.c_str()));
	}

	static Variant get_native_class(const String &p_name) {
		auto it = native_classes.find(p_name);
		if (it != native_classes.end()) {
			return Variant(it->second);
		}
		Ref<MexScriptNativeClass> ref;
		ref.instantiate();
		ref->set_name(p_name);
		native_classes[p_name] = ref;
		return Variant(ref);
	}

	// --- Identifiants ---
	Variant get_identifier(const MXNode *p_node) {
		const String &name = p_node->name;
		if (Variant *local = find_local(name)) {
			return *local;
		}
		if (instance) {
			auto it = script->member_indices.find(name);
			if (it != script->member_indices.end()) {
				return instance->members[it->second];
			}
		}
		{
			auto it = script->constants.find(name);
			if (it != script->constants.end()) {
				return it->second;
			}
		}
		if (owner) {
			if (script->parsed.function_map.count(name)) {
				return Variant(Callable(owner, name));
			}
			if (script->signal_names.count(name)) {
				return Variant(Signal(owner, name));
			}
			Variant ret;
			if (ClassDB::get_property(owner, name, ret)) {
				return ret;
			}
			if (owner->has_signal(name)) {
				return Variant(Signal(owner, name));
			}
			if (ClassDB::get_method(ClassDB::get_class_info(owner->get_class()), name)) {
				return Variant(Callable(owner, name));
			}
		}
		{
			int64_t c = 0;
			if (ClassDB::get_integer_constant(script->parsed.extends, name, c)) {
				return Variant(c);
			}
		}
		Variant global;
		if (mexscript_find_global_constant(name, global)) {
			return global;
		}
		if (Engine::get_singleton() && Engine::get_singleton()->has_singleton(name)) {
			return Variant(Engine::get_singleton()->get_singleton_object(name));
		}
		if (ClassDB::class_exists(name)) {
			return get_native_class(name);
		}
		if (name == "super") {
			fail("'super' ne peut être utilisé que pour appeler une méthode : super.methode().");
		}
		if (is_builtin_static_type(name) || mexscript_find_builtin(name)) {
			fail(vformat("'%s' est une fonction : ajoutez des parenthèses pour l'appeler.", name.c_str()));
		}
		fail(vformat("Identifiant '%s' non déclaré.", name.c_str()));
	}

	void set_identifier(const String &p_name, const Variant &p_value) {
		if (Variant *local = find_local(p_name)) {
			*local = p_value;
			return;
		}
		if (instance) {
			auto it = script->member_indices.find(p_name);
			if (it != script->member_indices.end()) {
				instance->members[it->second] = _convert_typed(script->member_info[it->second].type, p_value);
				return;
			}
		}
		if (script->constants.count(p_name)) {
			fail(vformat("Impossible de modifier la constante '%s'.", p_name.c_str()));
		}
		if (owner) {
			if (ClassDB::set_property(owner, p_name, p_value)) {
				return;
			}
		}
		fail(vformat("Identifiant '%s' non déclaré (affectation impossible).", p_name.c_str()));
	}

	// --- Opérateurs ---
	bool is_type(const Variant &p_value, const String &p_type) {
		Variant::Type t = Variant::get_type_by_name(p_type);
		if (t != Variant::VARIANT_MAX && t != Variant::OBJECT) {
			return p_value.get_type() == t;
		}
		if (p_value.get_type() != Variant::OBJECT) {
			return false;
		}
		Object *obj = p_value.as_object();
		if (!obj) {
			return false;
		}
		if (p_type == "Object") {
			return true;
		}
		if (ClassDB::class_exists(p_type)) {
			return ClassDB::is_parent_class(obj->get_class(), p_type);
		}
		MexScript *other = Object::cast_to<MexScript>(obj->get_script().as_object());
		if (other && other->parsed.class_name == p_type) {
			return true;
		}
		return false;
	}

	Variant binary_op(int p_op, const Variant &p_a, const Variant &p_b) {
		if (p_op < 100) {
			Variant ret;
			bool valid = false;
			Variant::evaluate((Variant::Operator)p_op, p_a, p_b, ret, valid);
			if (!valid) {
				if (p_op == Variant::OP_EQ) {
					return Variant(false);
				}
				if (p_op == Variant::OP_NE) {
					return Variant(true);
				}
				if ((p_op == Variant::OP_DIV || p_op == Variant::OP_MOD) && p_a.get_type() == Variant::INT && p_b.get_type() == Variant::INT) {
					fail("Division entière par zéro.");
				}
				fail(vformat("Opérandes invalides pour l'opérateur '%s' : %s et %s.", Variant::get_operator_name((Variant::Operator)p_op).c_str(), _describe(p_a).c_str(), _describe(p_b).c_str()));
			}
			return ret;
		}
		switch (p_op) {
			case MXNode::OP_NOT_IN: {
				Variant ret;
				bool valid = false;
				Variant::evaluate(Variant::OP_IN, p_a, p_b, ret, valid);
				if (!valid) {
					fail(vformat("Opérandes invalides pour l'opérateur 'in' : %s et %s.", _describe(p_a).c_str(), _describe(p_b).c_str()));
				}
				return Variant(!ret.booleanize());
			}
			case MXNode::OP_BIT_AND:
			case MXNode::OP_BIT_OR:
			case MXNode::OP_BIT_XOR:
			case MXNode::OP_SHIFT_LEFT:
			case MXNode::OP_SHIFT_RIGHT: {
				if (p_a.get_type() != Variant::INT || p_b.get_type() != Variant::INT) {
					fail(vformat("Les opérateurs bit à bit attendent des entiers (%s et %s fournis).", _describe(p_a).c_str(), _describe(p_b).c_str()));
				}
				const int64_t a = p_a.as_int();
				const int64_t b = p_b.as_int();
				switch (p_op) {
					case MXNode::OP_BIT_AND: return Variant(a & b);
					case MXNode::OP_BIT_OR: return Variant(a | b);
					case MXNode::OP_BIT_XOR: return Variant(a ^ b);
					case MXNode::OP_SHIFT_LEFT: return Variant((int64_t)(b >= 0 && b < 64 ? (uint64_t)a << b : 0));
					default: return Variant((int64_t)(b >= 0 && b < 64 ? a >> b : 0));
				}
			}
			case MXNode::OP_AND:
				return Variant(p_a.booleanize() && p_b.booleanize());
			case MXNode::OP_OR:
				return Variant(p_a.booleanize() || p_b.booleanize());
			default:
				fail("Opérateur non pris en charge.");
		}
	}

	// --- Appels ---
	Variant call_function(int p_index, const Variant **p_args, int p_argc, const Captures *p_captures = nullptr) {
		const MXFunction &f = script->parsed.functions[p_index];
		const int max_args = (int)f.params.size();
		const int min_args = max_args - (int)f.defaults.size();
		if (p_argc < min_args || p_argc > max_args) {
			if (min_args == max_args) {
				fail(vformat("La fonction '%s' attend %d argument(s), %d fourni(s).", f.name.c_str(), max_args, p_argc));
			}
			fail(vformat("La fonction '%s' attend entre %d et %d arguments, %d fourni(s).", f.name.c_str(), min_args, max_args, p_argc));
		}
		if (call_depth >= MAX_CALL_DEPTH) {
			fail(vformat("Dépassement de pile : trop d'appels imbriqués (récursion infinie dans '%s' ?).", f.name.c_str()));
		}

		Frame new_frame;
		new_frame.function = &f;
		new_frame.line = f.line;
		new_frame.locals.reserve((p_captures ? p_captures->size() : 0) + f.params.size() + 4);
		if (p_captures) {
			for (const auto &c : *p_captures) {
				new_frame.locals.push_back({ &c.first, c.second });
			}
		}
		for (int i = 0; i < p_argc; i++) {
			const String &type = f.param_types[i];
			new_frame.locals.push_back({ &f.params[i], type.empty() ? *p_args[i] : _convert_typed(_type_from_hint(type), *p_args[i]) });
		}

		struct Scope {
			MXInterpreter *interp;
			Frame *previous;
			Scope(MXInterpreter *p_interp, Frame *p_new) :
					interp(p_interp), previous(p_interp->frame) {
				interp->frame = p_new;
				call_depth++;
				MXStackEntry entry;
				entry.script = interp->script;
				entry.function = &p_new->function->name;
				entry.line = &p_new->line;
				mx_call_count++;
				if (mx_profiling) {
					entry.start_usec = OS::get_singleton()->get_ticks_usec();
				}
				mx_stack.push_back(entry);
			}
			~Scope() {
				const MXStackEntry entry = mx_stack.back();
				mx_stack.pop_back();
				if (entry.start_usec) {
					const uint64_t elapsed = OS::get_singleton()->get_ticks_usec() - entry.start_usec;
					const String source = entry.script->get_debug_path();
					MXProfile &profile = mx_profiles[source + "::" + *entry.function];
					if (profile.calls == 0) {
						profile.source = source;
						profile.function = *entry.function;
					}
					profile.calls++;
					profile.total_usec += elapsed;
					profile.self_usec += elapsed > entry.children_usec ? elapsed - entry.children_usec : 0;
					if (!mx_stack.empty()) {
						mx_stack.back().children_usec += elapsed;
					}
				}
				interp->frame = previous;
				call_depth--;
			}
		} scope(this, &new_frame);

		for (int i = p_argc; i < max_args; i++) {
			Variant value = eval(f.defaults[i - min_args].get());
			new_frame.locals.push_back({ &f.params[i], value });
		}
		if (f.body) {
			exec(f.body.get());
		}
		return new_frame.ret;
	}

	Variant call_callable(const Variant &p_callable, const Variant **p_args, int p_argc, const String &p_name) {
		if (p_callable.get_type() != Variant::CALLABLE) {
			fail(vformat("'%s' n'est pas une fonction (type %s).", p_name.c_str(), _describe(p_callable).c_str()));
		}
		Callable c = p_callable.as_callable();
		if (!c.is_valid()) {
			fail(vformat("Le Callable '%s' n'est plus valide (objet libéré ou méthode introuvable).", p_name.c_str()));
		}
		CallError ce;
		Variant ret = c.callp(p_args, p_argc, ce);
		check_call(ce, c.method.empty() ? p_name : c.method, "la cible du Callable");
		return ret;
	}

	Variant call_identifier(const String &p_name, const Variant **p_args, int p_argc) {
		if (Variant *local = find_local(p_name)) {
			Variant callee = *local;
			return call_callable(callee, p_args, p_argc, p_name);
		}
		{
			auto it = script->parsed.function_map.find(p_name);
			if (it != script->parsed.function_map.end()) {
				return call_function(it->second, p_args, p_argc);
			}
		}
		if (MXBuiltinFunction builtin = mexscript_find_builtin(p_name)) {
			MXBuiltinContext ctx;
			Variant ret = builtin(p_args, p_argc, ctx);
			if (!ctx.error.empty()) {
				fail(p_name + "() : " + ctx.error);
			}
			return ret;
		}
		if (owner) {
			MethodBind *bind = ClassDB::get_method(ClassDB::get_class_info(owner->get_class()), p_name);
			if (bind) {
				CallError ce;
				Variant ret = bind->call(owner, p_args, p_argc, ce);
				check_call(ce, p_name, owner->get_class());
				return ret;
			}
		}
		if (instance) {
			auto it = script->member_indices.find(p_name);
			if (it != script->member_indices.end()) {
				Variant callee = instance->members[it->second];
				return call_callable(callee, p_args, p_argc, p_name);
			}
		}
		if (ClassDB::class_exists(p_name)) {
			fail(vformat("Pour créer un objet, utilisez %s.new().", p_name.c_str()));
		}
		fail(vformat("Fonction '%s' introuvable.", p_name.c_str()));
	}

	Variant instantiate_class(const String &p_class) {
		if (!ClassDB::can_instantiate(p_class)) {
			fail(vformat("La classe '%s' ne peut pas être instanciée.", p_class.c_str()));
		}
		Object *obj = ClassDB::instantiate(p_class);
		if (!obj) {
			fail(vformat("Échec de la création d'un objet '%s'.", p_class.c_str()));
		}
		return Variant(obj);
	}

	Variant call_method_on(Variant &p_base, const String &p_method, const Variant **p_args, int p_argc) {
		if (p_base.get_type() == Variant::OBJECT) {
			Object *obj = p_base.as_object();
			if (!obj) {
				fail(vformat("Appel de la méthode '%s' sur une valeur nulle (ou un objet libéré).", p_method.c_str()));
			}
			if (obj == owner) {
				auto it = script->parsed.function_map.find(p_method);
				if (it != script->parsed.function_map.end()) {
					return call_function(it->second, p_args, p_argc);
				}
			}
			if (MexScriptNativeClass *native = Object::cast_to<MexScriptNativeClass>(obj)) {
				if (p_method == "new") {
					if (p_argc != 0) {
						fail("new() n'accepte aucun argument.");
					}
					return instantiate_class(native->get_name());
				}
				fail(vformat("La classe '%s' n'a pas de fonction statique '%s'.", native->get_name().c_str(), p_method.c_str()));
			}
			if (p_method == "new") {
				if (MexScript *other = Object::cast_to<MexScript>(obj)) {
					Variant created = other->instantiate_object();
					if (created.is_null()) {
						fail("Impossible d'instancier le script.");
					}
					return created;
				}
			}
			if (p_method == "free" && p_argc == 0) {
				if (obj == owner) {
					fail("Un objet ne peut pas se libérer lui-même pendant l'exécution de son script : utilisez queue_free().");
				}
				if (obj->is_ref_counted()) {
					fail("Les objets RefCounted sont libérés automatiquement.");
				}
				memdelete(obj);
				return Variant();
			}
			CallError ce;
			Variant ret = obj->callp(p_method, p_args, p_argc, ce);
			if (ce.error == CallError::CALL_ERROR_INVALID_METHOD) {
				// Propriété contenant un Callable ?
				bool valid = false;
				Variant prop = obj->get(p_method, &valid);
				if (valid && prop.get_type() == Variant::CALLABLE) {
					return call_callable(prop, p_args, p_argc, p_method);
				}
			}
			check_call(ce, p_method, obj->get_class());
			return ret;
		}
		if (p_base.get_type() == Variant::NIL) {
			fail(vformat("Appel de la méthode '%s' sur une valeur nulle.", p_method.c_str()));
		}
		CallError ce;
		Variant ret = p_base.call_method(p_method, p_args, p_argc, ce);
		if (ce.error == CallError::CALL_ERROR_INVALID_METHOD && p_base.get_type() == Variant::DICTIONARY) {
			bool valid = false;
			Variant entry = p_base.get_named(p_method, valid);
			if (valid && entry.get_type() == Variant::CALLABLE) {
				return call_callable(entry, p_args, p_argc, p_method);
			}
		}
		check_call(ce, p_method, Variant::get_type_name(p_base.get_type()));
		return ret;
	}

	struct Arguments {
		std::vector<Variant> values;
		std::vector<const Variant *> pointers;
		int count = 0;
		const Variant **ptr() { return pointers.empty() ? nullptr : pointers.data(); }
	};

	void eval_arguments(const MXNode *p_call, Arguments &r_args) {
		r_args.count = (int)p_call->children.size() - 1;
		r_args.values.resize(r_args.count);
		r_args.pointers.resize(r_args.count);
		for (int i = 0; i < r_args.count; i++) {
			r_args.values[i] = eval(p_call->children[i + 1].get());
		}
		for (int i = 0; i < r_args.count; i++) {
			r_args.pointers[i] = &r_args.values[i];
		}
	}

	Variant eval_call(const MXNode *p_node) {
		const MXNode *target = p_node->children[0].get();
		Arguments args;
		if (target->type == MXNode::IDENTIFIER) {
			eval_arguments(p_node, args);
			return call_identifier(target->name, args.ptr(), args.count);
		}
		if (target->type == MXNode::ATTRIBUTE) {
			const MXNode *base_node = target->children[0].get();
			if (base_node->type == MXNode::IDENTIFIER && !find_local(base_node->name)) {
				if (base_node->name == "super") {
					eval_arguments(p_node, args);
					if (!owner) {
						fail("'super' utilisé sans instance.");
					}
					MethodBind *bind = ClassDB::get_method(ClassDB::get_class_info(owner->get_class()), target->name);
					if (!bind) {
						return Variant(); // rien à appeler dans la classe native (ex. : super._ready())
					}
					CallError ce;
					Variant ret = bind->call(owner, args.ptr(), args.count, ce);
					check_call(ce, target->name, owner->get_class());
					return ret;
				}
				if (is_builtin_static_type(base_node->name)) {
					eval_arguments(p_node, args);
					return call_builtin_type_static(base_node->name, target->name, args.ptr(), args.count);
				}
			}
			Variant base = eval(base_node);
			eval_arguments(p_node, args);
			return call_method_on(base, target->name, args.ptr(), args.count);
		}
		Variant callee = eval(target);
		eval_arguments(p_node, args);
		return call_callable(callee, args.ptr(), args.count, "<expression>");
	}

	// --- Accès ---
	Variant get_attribute(const Variant &p_base, const String &p_name) {
		if (p_base.get_type() == Variant::OBJECT) {
			Object *obj = p_base.as_object();
			if (!obj) {
				fail(vformat("Accès à la propriété '%s' sur une valeur nulle (ou un objet libéré).", p_name.c_str()));
			}
			if (obj == owner && instance) {
				auto it = script->member_indices.find(p_name);
				if (it != script->member_indices.end()) {
					return instance->members[it->second];
				}
			}
			if (MexScriptNativeClass *native = Object::cast_to<MexScriptNativeClass>(obj)) {
				int64_t c = 0;
				if (ClassDB::get_integer_constant(native->get_name(), p_name, c)) {
					return Variant(c);
				}
				fail(vformat("La classe '%s' n'a pas de constante '%s'.", native->get_name().c_str(), p_name.c_str()));
			}
		} else if (p_base.get_type() == Variant::NIL) {
			fail(vformat("Accès à la propriété '%s' sur une valeur nulle.", p_name.c_str()));
		}
		bool valid = false;
		Variant ret = p_base.get_named(p_name, valid);
		if (!valid) {
			if (p_base.get_type() == Variant::OBJECT) {
				// Constante de classe : noeud.NOTIFICATION_READY
				int64_t c = 0;
				if (ClassDB::get_integer_constant(p_base.as_object()->get_class(), p_name, c)) {
					return Variant(c);
				}
			}
			fail(vformat("Propriété '%s' introuvable dans %s.", p_name.c_str(), _describe(p_base).c_str()));
		}
		return ret;
	}

	void set_attribute(Variant &p_base, const String &p_name, const Variant &p_value) {
		if (p_base.get_type() == Variant::OBJECT) {
			Object *obj = p_base.as_object();
			if (!obj) {
				fail(vformat("Affectation de la propriété '%s' sur une valeur nulle (ou un objet libéré).", p_name.c_str()));
			}
			if (obj == owner && instance) {
				auto it = script->member_indices.find(p_name);
				if (it != script->member_indices.end()) {
					instance->members[it->second] = _convert_typed(script->member_info[it->second].type, p_value);
					return;
				}
			}
		} else if (p_base.get_type() == Variant::NIL) {
			fail(vformat("Affectation de la propriété '%s' sur une valeur nulle.", p_name.c_str()));
		}
		bool valid = false;
		p_base.set_named(p_name, p_value, valid);
		if (!valid) {
			fail(vformat("Impossible d'affecter la propriété '%s' de %s (propriété introuvable ou type incompatible).", p_name.c_str(), _describe(p_base).c_str()));
		}
	}

	Variant get_index(const Variant &p_base, const Variant &p_index) {
		bool valid = false;
		Variant ret = p_base.get_indexed(p_index, valid);
		if (!valid) {
			if (p_base.get_type() == Variant::ARRAY && p_index.is_num()) {
				fail(vformat("Index %d hors limites (taille du tableau : %d).", (int)p_index.as_int(), (int)p_base.as_array().size()));
			}
			if (p_base.get_type() == Variant::DICTIONARY) {
				fail(vformat("Clé %s absente du dictionnaire.", p_index.stringify().c_str()));
			}
			fail(vformat("Indexation invalide de %s avec %s.", _describe(p_base).c_str(), _describe(p_index).c_str()));
		}
		return ret;
	}

	void set_index(Variant &p_base, const Variant &p_index, const Variant &p_value) {
		bool valid = false;
		p_base.set_indexed(p_index, p_value, valid);
		if (!valid) {
			if (p_base.get_type() == Variant::ARRAY && p_index.is_num()) {
				fail(vformat("Index %d hors limites (taille du tableau : %d).", (int)p_index.as_int(), (int)p_base.as_array().size()));
			}
			fail(vformat("Affectation indexée invalide sur %s avec %s.", _describe(p_base).c_str(), _describe(p_index).c_str()));
		}
	}

	static bool is_assignable(const MXNode *p_node) {
		return p_node->type == MXNode::IDENTIFIER || p_node->type == MXNode::ATTRIBUTE || p_node->type == MXNode::INDEX;
	}

	// Affectation simple (utilisée aussi pour réécrire les types valeur : position.x = 3).
	void store(const MXNode *p_target, const Variant &p_value) {
		switch (p_target->type) {
			case MXNode::IDENTIFIER:
				set_identifier(p_target->name, p_value);
				break;
			case MXNode::ATTRIBUTE: {
				const MXNode *base_node = p_target->children[0].get();
				Variant base = eval(base_node);
				set_attribute(base, p_target->name, p_value);
				if (_is_value_type(base.get_type()) && is_assignable(base_node)) {
					store(base_node, base);
				}
			} break;
			case MXNode::INDEX: {
				const MXNode *base_node = p_target->children[0].get();
				Variant base = eval(base_node);
				Variant index = eval(p_target->children[1].get());
				set_index(base, index, p_value);
				if (_is_value_type(base.get_type()) && is_assignable(base_node)) {
					store(base_node, base);
				}
			} break;
			default:
				fail("Cible d'affectation invalide.");
		}
	}

	void exec_assign(const MXNode *p_node) {
		const MXNode *target = p_node->children[0].get();
		const MXNode *value_node = p_node->children[1].get();
		const int op = p_node->op;
		if (op < 0) {
			Variant value = eval(value_node);
			store(target, value);
			return;
		}
		// Affectation composée : la base et l'index ne sont évalués qu'une fois.
		switch (target->type) {
			case MXNode::IDENTIFIER: {
				Variant current = get_identifier(target);
				Variant value = eval(value_node);
				set_identifier(target->name, binary_op(op, current, value));
			} break;
			case MXNode::ATTRIBUTE: {
				const MXNode *base_node = target->children[0].get();
				Variant base = eval(base_node);
				Variant current = get_attribute(base, target->name);
				Variant value = eval(value_node);
				set_attribute(base, target->name, binary_op(op, current, value));
				if (_is_value_type(base.get_type()) && is_assignable(base_node)) {
					store(base_node, base);
				}
			} break;
			case MXNode::INDEX: {
				const MXNode *base_node = target->children[0].get();
				Variant base = eval(base_node);
				Variant index = eval(target->children[1].get());
				Variant current = get_index(base, index);
				Variant value = eval(value_node);
				set_index(base, index, binary_op(op, current, value));
				if (_is_value_type(base.get_type()) && is_assignable(base_node)) {
					store(base_node, base);
				}
			} break;
			default:
				fail("Cible d'affectation invalide.");
		}
	}

	// --- Expressions ---
	Variant eval(const MXNode *p_node) {
		switch (p_node->type) {
			case MXNode::LITERAL:
				return p_node->value;
			case MXNode::IDENTIFIER:
				return get_identifier(p_node);
			case MXNode::SELF:
				if (!owner) {
					fail("'self' n'est pas disponible ici (aucune instance).");
				}
				return Variant(owner);
			case MXNode::ARRAY: {
				Array arr;
				for (const MXNodeRef &c : p_node->children) {
					arr.push_back(eval(c.get()));
				}
				return Variant(arr);
			}
			case MXNode::DICTIONARY: {
				Dictionary dict;
				for (size_t i = 0; i + 1 < p_node->children.size(); i += 2) {
					Variant key = eval(p_node->children[i].get());
					Variant value = eval(p_node->children[i + 1].get());
					dict[key] = value;
				}
				return Variant(dict);
			}
			case MXNode::UNARY: {
				Variant a = eval(p_node->children[0].get());
				switch (p_node->op) {
					case MXNode::OP_NOT:
						return Variant(!a.booleanize());
					case MXNode::OP_POSITIVE:
						return a;
					case MXNode::OP_BIT_NOT:
						if (a.get_type() != Variant::INT) {
							fail("L'opérateur '~' attend un entier.");
						}
						return Variant(~a.as_int());
					default: {
						Variant ret;
						bool valid = false;
						Variant::evaluate(Variant::OP_NEG, a, Variant(), ret, valid);
						if (!valid) {
							fail(vformat("Opérande invalide pour l'opérateur '-' unaire : %s.", _describe(a).c_str()));
						}
						return ret;
					}
				}
			}
			case MXNode::BINARY: {
				const int op = p_node->op;
				if (op == MXNode::OP_AND) {
					Variant a = eval(p_node->children[0].get());
					if (!a.booleanize()) {
						return Variant(false);
					}
					return Variant(eval(p_node->children[1].get()).booleanize());
				}
				if (op == MXNode::OP_OR) {
					Variant a = eval(p_node->children[0].get());
					if (a.booleanize()) {
						return Variant(true);
					}
					return Variant(eval(p_node->children[1].get()).booleanize());
				}
				if (op == MXNode::OP_IS || op == MXNode::OP_IS_NOT) {
					Variant a = eval(p_node->children[0].get());
					const bool result = is_type(a, p_node->children[1]->name);
					return Variant(op == MXNode::OP_IS ? result : !result);
				}
				Variant a = eval(p_node->children[0].get());
				Variant b = eval(p_node->children[1].get());
				return binary_op(op, a, b);
			}
			case MXNode::TERNARY: {
				Variant cond = eval(p_node->children[0].get());
				return eval(p_node->children[cond.booleanize() ? 1 : 2].get());
			}
			case MXNode::CALL:
				return eval_call(p_node);
			case MXNode::ATTRIBUTE: {
				const MXNode *base_node = p_node->children[0].get();
				if (base_node->type == MXNode::IDENTIFIER && is_builtin_static_type(base_node->name) && !find_local(base_node->name)) {
					Variant ret;
					if (!get_builtin_type_constant(base_node->name, p_node->name, ret)) {
						fail(vformat("Constante '%s.%s' introuvable.", base_node->name.c_str(), p_node->name.c_str()));
					}
					return ret;
				}
				Variant base = eval(base_node);
				return get_attribute(base, p_node->name);
			}
			case MXNode::INDEX: {
				Variant base = eval(p_node->children[0].get());
				Variant index = eval(p_node->children[1].get());
				return get_index(base, index);
			}
			case MXNode::GET_NODE: {
				if (!owner) {
					fail("'$' n'est pas disponible ici (aucune instance).");
				}
				Variant path(p_node->name);
				const Variant *argp[1] = { &path };
				CallError ce;
				Variant ret = owner->callp("get_node", argp, 1, ce);
				if (ce.error != CallError::CALL_OK) {
					fail("'$' n'est utilisable que dans un script attaché à un Node.");
				}
				return ret;
			}
			case MXNode::LAMBDA: {
				auto captures = std::make_shared<Captures>();
				if (frame) {
					captures->reserve(frame->locals.size());
					for (const Local &l : frame->locals) {
						captures->emplace_back(*l.name, l.value);
					}
				}
				Ref<MexScript> script_ref(script);
				const ObjectID owner_id = owner ? owner->get_instance_id() : 0;
				const int function_index = p_node->function_index;
				return Variant(Callable::from_function([script_ref, owner_id, function_index, captures](const Variant **p_args, int p_argc) -> Variant {
					return MexScript::_call_lambda(script_ref, owner_id, function_index, *captures, p_args, p_argc);
				}));
			}
			default:
				fail("Expression non prise en charge.");
		}
	}

	// --- Instructions ---
	Flow exec_block(const MXNode *p_block) {
		const size_t base = frame->locals.size();
		Flow flow = FLOW_NORMAL;
		for (const MXNodeRef &c : p_block->children) {
			flow = exec(c.get());
			if (flow != FLOW_NORMAL) {
				break;
			}
		}
		frame->locals.erase(frame->locals.begin() + base, frame->locals.end());
		return flow;
	}

	Flow exec_for(const MXNode *p_node) {
		Variant iterable = eval(p_node->children[0].get());
		const MXNode *body = p_node->children[1].get();
		const size_t base = frame->locals.size();
		frame->locals.push_back({ &p_node->name, Variant() });
		Flow result = FLOW_NORMAL;

		auto step = [&](const Variant &p_value) -> bool {
			frame->locals[base].value = p_value;
			Flow flow = exec(body);
			if (flow == FLOW_BREAK) {
				return false;
			}
			if (flow == FLOW_RETURN) {
				result = FLOW_RETURN;
				return false;
			}
			return true;
		};

		switch (iterable.get_type()) {
			case Variant::INT:
			case Variant::FLOAT: {
				const int64_t count = iterable.as_int();
				for (int64_t i = 0; i < count; i++) {
					if (!step(Variant(i))) {
						break;
					}
				}
			} break;
			case Variant::ARRAY: {
				Array arr = iterable.as_array();
				for (int64_t i = 0; i < (int64_t)arr.size(); i++) {
					Variant item = arr[(int)i];
					if (!step(item)) {
						break;
					}
				}
			} break;
			case Variant::DICTIONARY: {
				Array keys = iterable.as_dictionary().keys();
				for (int64_t i = 0; i < (int64_t)keys.size(); i++) {
					Variant key = keys[(int)i];
					if (!step(key)) {
						break;
					}
				}
			} break;
			case Variant::STRING: {
				const std::vector<uint32_t> chars = utf8_decode(iterable.as_string());
				for (uint32_t ch : chars) {
					if (!step(Variant(utf8_encode(ch)))) {
						break;
					}
				}
			} break;
			default:
				frame->locals.erase(frame->locals.begin() + base, frame->locals.end());
				fail(vformat("Impossible d'itérer sur une valeur de type %s.", _describe(iterable).c_str()));
		}
		frame->locals.erase(frame->locals.begin() + base, frame->locals.end());
		return result;
	}

	Flow exec(const MXNode *p_node) {
		if (p_node->line > 0) {
			frame->line = p_node->line;
		}
		switch (p_node->type) {
			case MXNode::BLOCK:
				return exec_block(p_node);
			case MXNode::VAR: {
				Variant value;
				const Variant::Type declared = p_node->value.get_type() == Variant::STRING ? _type_from_hint(p_node->value.as_string()) : Variant::NIL;
				if (!p_node->children.empty()) {
					value = _convert_typed(declared, eval(p_node->children[0].get()));
				} else {
					value = _default_for_type(declared);
				}
				frame->locals.push_back({ &p_node->name, value });
				return FLOW_NORMAL;
			}
			case MXNode::ASSIGN:
				exec_assign(p_node);
				return FLOW_NORMAL;
			case MXNode::IF: {
				Variant cond = eval(p_node->children[0].get());
				if (cond.booleanize()) {
					return exec(p_node->children[1].get());
				}
				if (p_node->children.size() > 2) {
					return exec(p_node->children[2].get());
				}
				return FLOW_NORMAL;
			}
			case MXNode::WHILE: {
				while (eval(p_node->children[0].get()).booleanize()) {
					Flow flow = exec(p_node->children[1].get());
					if (flow == FLOW_BREAK) {
						break;
					}
					if (flow == FLOW_RETURN) {
						return FLOW_RETURN;
					}
				}
				return FLOW_NORMAL;
			}
			case MXNode::FOR:
				return exec_for(p_node);
			case MXNode::MATCH: {
				Variant subject = eval(p_node->children[0].get());
				for (size_t i = 1; i + 1 < p_node->children.size(); i += 2) {
					const MXNode *patterns = p_node->children[i].get();
					bool matched = patterns->children.empty(); // "_"
					for (const MXNodeRef &pattern : patterns->children) {
						Variant candidate = eval(pattern.get());
						if (binary_op(Variant::OP_EQ, subject, candidate).booleanize()) {
							matched = true;
							break;
						}
					}
					if (matched) {
						return exec(p_node->children[i + 1].get());
					}
				}
				return FLOW_NORMAL;
			}
			case MXNode::RETURN:
				frame->ret = p_node->children.empty() ? Variant() : eval(p_node->children[0].get());
				return FLOW_RETURN;
			case MXNode::BREAK:
				return FLOW_BREAK;
			case MXNode::CONTINUE:
				return FLOW_CONTINUE;
			case MXNode::PASS:
				return FLOW_NORMAL;
			case MXNode::EXPRESSION:
				eval(p_node->children[0].get());
				return FLOW_NORMAL;
			default:
				// Expression utilisée comme instruction.
				eval(p_node);
				return FLOW_NORMAL;
		}
	}

	// --- Points d'entrée protégés (les erreurs sont journalisées) ---
	Variant run(int p_function, const Variant **p_args, int p_argc, const Captures *p_captures = nullptr, bool *r_ok = nullptr) {
		Ref<MexScript> keep_alive(script);
		if (r_ok) {
			*r_ok = true;
		}
		try {
			return call_function(p_function, p_args, p_argc, p_captures);
		} catch (const MXError &err) {
			String function = err.function;
			int line = err.line;
			if (line == 0 && p_function >= 0 && p_function < (int)script->parsed.functions.size()) {
				line = script->parsed.functions[p_function].line;
				function = script->parsed.functions[p_function].name;
			}
			script->report_runtime_error(line, function, err.message + err.stack);
			if (r_ok) {
				*r_ok = false;
			}
		}
		return Variant();
	}

	// Évalue une expression hors fonction (valeur initiale d'un membre, constante).
	bool eval_toplevel(const MXNode *p_node, int p_line, Variant &r_value, String *r_error = nullptr) {
		Frame top;
		top.line = p_line;
		Frame *previous = frame;
		frame = &top;
		bool ok = true;
		try {
			r_value = eval(p_node);
		} catch (const MXError &err) {
			ok = false;
			if (r_error) {
				*r_error = err.message;
			} else {
				script->report_runtime_error(err.line ? err.line : p_line, err.function, err.message);
			}
		}
		frame = previous;
		return ok;
	}
};

// ---------------------------------------------------------------------------
// MexScript
// ---------------------------------------------------------------------------
void MexScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("new"), &MexScript::instantiate_object);
	ClassDB::bind_method(D_METHOD("get_error_message"), &MexScript::get_error_message);
	ClassDB::bind_method(D_METHOD("get_error_line"), &MexScript::get_error_line);
}

String MexScript::get_debug_path() const {
	if (!get_path().empty()) {
		return get_path();
	}
	return debug_path.empty() ? String("<script>") : debug_path;
}

void MexScript::report_runtime_error(int p_line, const String &p_function, const String &p_message) {
	String text = vformat("%s:%d", get_debug_path().c_str(), p_line);
	if (!p_function.empty()) {
		text += " (" + p_function + ")";
	}
	text += " - " + p_message;
	if (text == last_runtime_error) {
		return; // évite d'inonder la console quand l'erreur se répète à chaque image
	}
	last_runtime_error = text;
	print_error("Script : " + text);
}

Variant MexScript::_call_lambda(const Ref<MexScript> &p_script, ObjectID p_owner, int p_function, const std::vector<std::pair<String, Variant>> &p_captures, const Variant **p_args, int p_argc) {
	if (p_script.is_null() || !p_script->valid || p_function < 0 || p_function >= (int)p_script->parsed.functions.size()) {
		return Variant();
	}
	Object *obj = nullptr;
	MexScriptInstance *inst = nullptr;
	if (p_owner != 0) {
		obj = ObjectDB::get_instance(p_owner);
		if (!obj) {
			return Variant(); // le propriétaire a été libéré
		}
		inst = dynamic_cast<MexScriptInstance *>(obj->get_script_instance());
		if (inst && inst->script.ptr() != p_script.ptr()) {
			inst = nullptr;
		}
	}
	MXInterpreter interp(p_script.ptr(), inst, obj);
	return interp.run(p_function, p_args, p_argc, &p_captures);
}

Error MexScript::reload() {
	// Mémorise les valeurs des instances existantes pour les restaurer par nom.
	std::vector<String> old_names;
	for (const MXMember &m : parsed.members) {
		old_names.push_back(m.name);
	}

	valid = false;
	constants.clear();
	member_indices.clear();
	member_info.clear();
	signal_names.clear();
	has_onready = false;
	last_runtime_error.clear();
	parsed = MXParseResult();

	MexScriptParser::parse(source, parsed);
	if (!parsed.has_error && !ClassDB::class_exists(parsed.extends)) {
		parsed.has_error = true;
		parsed.error = "Classe de base inconnue : '" + parsed.extends + "'.";
		parsed.error_line = 1;
	}

	if (!parsed.has_error) {
		MXInterpreter interp(this, nullptr, nullptr);
		for (const MXConstant &c : parsed.constants) {
			Variant value;
			String error;
			if (c.init && !interp.eval_toplevel(c.init.get(), c.line, value, &error)) {
				parsed.has_error = true;
				parsed.error = "Constante '" + c.name + "' : " + error;
				parsed.error_line = c.line;
				break;
			}
			constants[c.name] = value;
		}
	}

	if (parsed.has_error) {
		print_error(vformat("Script : %s:%d - %s", get_debug_path().c_str(), parsed.error_line, parsed.error.c_str()));
		// Vide le contenu partiel : plus rien n'est appelable.
		MXParseResult failed;
		failed.has_error = true;
		failed.error = parsed.error;
		failed.error_line = parsed.error_line;
		failed.extends = ClassDB::class_exists(parsed.extends) ? parsed.extends : String("Node");
		parsed = failed;
		constants.clear();
		for (MexScriptInstance *inst : instances) {
			inst->members.clear();
		}
		return ERR_PARSE_ERROR;
	}

	for (const MethodInfo &s : parsed.signals) {
		signal_names.insert(s.name);
	}

	MXInterpreter interp(this, nullptr, nullptr);
	for (size_t i = 0; i < parsed.members.size(); i++) {
		const MXMember &m = parsed.members[i];
		member_indices[m.name] = (int)i;
		MemberInfo mi;
		mi.exported = m.exported;
		mi.type = _type_from_hint(m.type_hint);
		mi.default_value = _default_for_type(mi.type);
		if (m.onready) {
			has_onready = true;
		}
		if (m.exported) {
			if (m.init) {
				Variant value;
				String error;
				if (interp.eval_toplevel(m.init.get(), m.line, value, &error)) {
					mi.default_value = _convert_typed(mi.type, value);
				}
			}
			Variant::Type type = mi.type;
			if (type == Variant::NIL) {
				type = mi.default_value.get_type();
				if (m.hint == PROPERTY_HINT_ENUM || m.hint == PROPERTY_HINT_RANGE) {
					if (type == Variant::NIL) {
						type = Variant::INT;
					}
				}
				// Le type déduit devient le type du membre (comme `:=`).
				if (type != Variant::NIL && type != Variant::OBJECT) {
					mi.type = type;
				}
			}
			PropertyHint hint = m.hint;
			String hint_string = m.hint_string;
			if (type == Variant::OBJECT && hint == PROPERTY_HINT_NONE && ClassDB::class_exists(m.type_hint) && ClassDB::is_parent_class(m.type_hint, "Resource")) {
				hint = PROPERTY_HINT_RESOURCE_TYPE;
				hint_string = m.type_hint;
			}
			mi.info = PropertyInfo(type, m.name, hint, hint_string, PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SCRIPT_VARIABLE);
		} else {
			mi.info = PropertyInfo(mi.type, m.name, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_SCRIPT_VARIABLE);
		}
		member_info.push_back(mi);
	}

	valid = true;

	// Réaligne les instances existantes sur la nouvelle disposition des membres.
	for (MexScriptInstance *inst : instances) {
		std::vector<Variant> old_values = inst->members;
		inst->members.assign(parsed.members.size(), Variant());
		inst->_init_members(false);
		for (size_t i = 0; i < old_names.size() && i < old_values.size(); i++) {
			auto it = member_indices.find(old_names[i]);
			if (it != member_indices.end()) {
				inst->members[it->second] = _convert_typed(member_info[it->second].type, old_values[i]);
			}
		}
	}
	return OK;
}

ScriptInstance *MexScript::instance_create(Object *p_owner) {
	ERR_FAIL_NULL_V(p_owner, nullptr);
	if (!valid) {
		print_error(vformat("Script : %s n'est pas valide et ne sera pas exécuté.", get_debug_path().c_str()));
		return nullptr;
	}
	if (!ClassDB::is_parent_class(p_owner->get_class(), parsed.extends)) {
		print_error(vformat("Script : %s étend '%s' et ne peut pas être attaché à un objet de type '%s'.", get_debug_path().c_str(), parsed.extends.c_str(), p_owner->get_class().c_str()));
		return nullptr;
	}
	MexScriptInstance *inst = new MexScriptInstance;
	inst->owner = p_owner;
	inst->script = Ref<MexScript>(this);
	inst->members.assign(parsed.members.size(), Variant());
	instances.insert(inst);
	inst->_init_members(false);

	auto it = parsed.function_map.find("_init");
	if (it != parsed.function_map.end()) {
		MXInterpreter interp(this, inst, p_owner);
		interp.run(it->second, nullptr, 0);
	}
	return inst;
}

bool MexScript::has_method(const String &p_method) const {
	return parsed.function_map.count(p_method) > 0;
}

void MexScript::get_method_list(std::vector<String> *p_list) const {
	for (const MXFunction &f : parsed.functions) {
		if (parsed.function_map.count(f.name)) {
			p_list->push_back(f.name);
		}
	}
}

void MexScript::get_script_signal_list(std::vector<MethodInfo> *p_list) const {
	for (const MethodInfo &s : parsed.signals) {
		p_list->push_back(s);
	}
}

void MexScript::get_script_property_list(std::vector<PropertyInfo> *p_list) const {
	bool category_added = false;
	for (const MemberInfo &mi : member_info) {
		if (!mi.exported) {
			continue;
		}
		if (!category_added) {
			p_list->push_back(PropertyInfo(Variant::NIL, "Variables du script", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CATEGORY));
			category_added = true;
		}
		p_list->push_back(mi.info);
	}
}

bool MexScript::get_property_default_value(const String &p_name, Variant &r_value) const {
	auto it = member_indices.find(p_name);
	if (it == member_indices.end() || !member_info[it->second].exported) {
		return false;
	}
	r_value = member_info[it->second].default_value;
	return true;
}

Variant MexScript::instantiate_object() {
	if (!valid) {
		print_error(vformat("Script : impossible d'instancier %s (script invalide).", get_debug_path().c_str()));
		return Variant();
	}
	if (!ClassDB::can_instantiate(parsed.extends)) {
		print_error(vformat("Script : la classe de base '%s' ne peut pas être instanciée.", parsed.extends.c_str()));
		return Variant();
	}
	Object *obj = ClassDB::instantiate(parsed.extends);
	ERR_FAIL_NULL_V(obj, Variant());
	Variant ret(obj); // retient une référence si l'objet est RefCounted
	obj->set_script(Variant(Ref<MexScript>(this)));
	return ret;
}

// ---------------------------------------------------------------------------
// MexScriptInstance
// ---------------------------------------------------------------------------
MexScriptInstance::~MexScriptInstance() {
	if (script.is_valid()) {
		script->instances.erase(this);
	}
}

void MexScriptInstance::_init_members(bool p_onready) {
	MXInterpreter interp(script.ptr(), this, owner);
	const std::vector<MXMember> &list = script->parsed.members;
	for (size_t i = 0; i < list.size() && i < members.size(); i++) {
		const MXMember &m = list[i];
		if (m.onready != p_onready) {
			continue;
		}
		const MexScript::MemberInfo &mi = script->member_info[i];
		Variant value = _default_for_type(mi.type);
		if (m.init) {
			Variant computed;
			if (interp.eval_toplevel(m.init.get(), m.line, computed)) {
				value = _convert_typed(mi.type, computed);
			}
		}
		members[i] = value;
	}
}

bool MexScriptInstance::set(const String &p_name, const Variant &p_value) {
	auto it = script->member_indices.find(p_name);
	if (it == script->member_indices.end() || it->second >= (int)members.size()) {
		return false;
	}
	members[it->second] = _convert_typed(script->member_info[it->second].type, p_value);
	return true;
}

bool MexScriptInstance::get(const String &p_name, Variant &r_value) const {
	auto it = script->member_indices.find(p_name);
	if (it != script->member_indices.end() && it->second < (int)members.size()) {
		r_value = members[it->second];
		return true;
	}
	auto c = script->constants.find(p_name);
	if (c != script->constants.end()) {
		r_value = c->second;
		return true;
	}
	return false;
}

void MexScriptInstance::get_property_list(std::vector<PropertyInfo> *p_list) const {
	script->get_script_property_list(p_list);
}

bool MexScriptInstance::has_method(const String &p_method) const {
	if (script->parsed.function_map.count(p_method)) {
		return true;
	}
	return p_method == "_ready" && script->has_onready;
}

Variant MexScriptInstance::callp(const String &p_method, const Variant **p_args, int p_argc, CallError &r_error) {
	r_error.error = CallError::CALL_OK;
	if (!script->valid) {
		r_error.error = CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}
	Ref<MexScript> keep_alive = script;
	if (p_method == "_ready" && script->has_onready && !onready_done) {
		onready_done = true;
		_init_members(true);
	}
	auto it = script->parsed.function_map.find(p_method);
	if (it == script->parsed.function_map.end()) {
		if (p_method != "_ready" || !script->has_onready) {
			r_error.error = CallError::CALL_ERROR_INVALID_METHOD;
		}
		return Variant();
	}
	MXInterpreter interp(script.ptr(), this, owner);
	return interp.run(it->second, p_args, p_argc);
}

// ---------------------------------------------------------------------------
// Chargement
// ---------------------------------------------------------------------------
Ref<Resource> ResourceFormatLoaderMexScript::load(const String &p_path, Error *r_error) {
	Error err = OK;
	String code = FileAccess::get_file_as_string(p_path, &err);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}
	Ref<MexScript> script;
	script.instantiate();
	script->set_debug_path(p_path);
	script->set_source_code(code);
	script->reload(); // un script invalide reste chargé pour pouvoir être corrigé dans l'éditeur
	return script;
}

static ResourceFormatLoaderMexScript *mexscript_loader = nullptr;

static void _mx_get_stack(std::vector<Performance::ScriptStackEntry> *r_stack) {
	for (int i = (int)mx_stack.size() - 1; i >= 0; i--) {
		Performance::ScriptStackEntry entry;
		entry.source = mx_stack[(size_t)i].script->get_debug_path();
		entry.function = *mx_stack[(size_t)i].function;
		entry.line = *mx_stack[(size_t)i].line;
		r_stack->push_back(entry);
	}
}

static void _mx_profiling_set(bool p_enabled) {
	mx_profiling = p_enabled;
}

static bool _mx_profiling_get() {
	return mx_profiling;
}

static void _mx_profiling_data(std::vector<Performance::ScriptProfileEntry> *r_data, bool p_reset) {
	for (const auto &kv : mx_profiles) {
		Performance::ScriptProfileEntry entry;
		entry.source = kv.second.source;
		entry.function = kv.second.function;
		entry.calls = kv.second.calls;
		entry.total_usec = kv.second.total_usec;
		entry.self_usec = kv.second.self_usec;
		r_data->push_back(entry);
	}
	if (p_reset) {
		mx_profiles.clear();
	}
}

void register_mexscript_types() {
	Performance::script_get_stack = &_mx_get_stack;
	Performance::script_profiling_set_enabled = &_mx_profiling_set;
	Performance::script_profiling_is_enabled = &_mx_profiling_get;
	Performance::script_profiling_get_data = &_mx_profiling_data;
	if (Performance::get_singleton()) {
		Performance::get_singleton()->set_monitor_provider(Performance::SCRIPT_CALL_COUNT, []() {
			const double count = (double)mx_call_count;
			mx_call_count = 0;
			return count;
		});
	}
	GDREGISTER_CLASS(MexScriptNativeClass);
	GDREGISTER_CLASS(MexScript);
	mexscript_loader = new ResourceFormatLoaderMexScript;
	ResourceLoader::add_loader(mexscript_loader);
}

void unregister_mexscript_types() {
	native_classes.clear();
}
