#include "tween.h"

#include "core/math/math_defs.h"
#include "scene/animation/animation_player.h"
#include "scene/main/node.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Courbes d'accélération (équations de Robert Penner, forme normalisée 0..1)
// ---------------------------------------------------------------------------
static double _ease_in(int p_trans, double t) {
	switch (p_trans) {
		case Tween::TRANS_SINE:
			return 1.0 - std::cos(t * Math_PI * 0.5);
		case Tween::TRANS_QUAD:
			return t * t;
		case Tween::TRANS_CUBIC:
			return t * t * t;
		case Tween::TRANS_QUART:
			return t * t * t * t;
		case Tween::TRANS_QUINT:
			return t * t * t * t * t;
		case Tween::TRANS_EXPO:
			return t <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * (t - 1.0));
		case Tween::TRANS_CIRC:
			return 1.0 - std::sqrt(std::max(0.0, 1.0 - t * t));
		case Tween::TRANS_BACK: {
			const double s = 1.70158;
			return t * t * ((s + 1.0) * t - s);
		}
		case Tween::TRANS_ELASTIC: {
			if (t <= 0.0) {
				return 0.0;
			}
			if (t >= 1.0) {
				return 1.0;
			}
			const double p = 0.3;
			return -std::pow(2.0, 10.0 * (t - 1.0)) * std::sin((t - 1.0 - p / 4.0) * Math_TAU / p);
		}
		case Tween::TRANS_BOUNCE: {
			// Rebond : la forme « sortie » est la plus simple à écrire ; l'entrée en est le miroir.
			double u = 1.0 - t;
			double r;
			if (u < 1.0 / 2.75) {
				r = 7.5625 * u * u;
			} else if (u < 2.0 / 2.75) {
				u -= 1.5 / 2.75;
				r = 7.5625 * u * u + 0.75;
			} else if (u < 2.5 / 2.75) {
				u -= 2.25 / 2.75;
				r = 7.5625 * u * u + 0.9375;
			} else {
				u -= 2.625 / 2.75;
				r = 7.5625 * u * u + 0.984375;
			}
			return 1.0 - r;
		}
		default:
			return t;
	}
}

double Tween::run_equation(int p_trans, int p_ease, double p_t) {
	const double t = Math::clamp(p_t, 0.0, 1.0);
	if (p_trans <= TRANS_LINEAR || p_trans >= TRANS_MAX) {
		return t;
	}
	switch (p_ease) {
		case EASE_IN:
			return _ease_in(p_trans, t);
		case EASE_OUT:
			return 1.0 - _ease_in(p_trans, 1.0 - t);
		case EASE_OUT_IN:
			return t < 0.5 ? 0.5 * (1.0 - _ease_in(p_trans, 1.0 - t * 2.0)) : 0.5 + 0.5 * _ease_in(p_trans, t * 2.0 - 1.0);
		default: // EASE_IN_OUT
			return t < 0.5 ? 0.5 * _ease_in(p_trans, t * 2.0) : 1.0 - 0.5 * _ease_in(p_trans, 2.0 - t * 2.0);
	}
}

Variant Tween::interpolate_value(const Variant &p_from, const Variant &p_to, double p_weight) {
	if (p_from.get_type() == Variant::INT && p_to.get_type() == Variant::INT) {
		return Variant((int64_t)std::llround(Math::lerp(p_from.as_float(), p_to.as_float(), p_weight)));
	}
	if (p_from.get_type() == Variant::BOOL || p_from.get_type() == Variant::STRING) {
		return p_weight >= 1.0 ? p_to : p_from;
	}
	return AnimationPlayer::interpolate(p_from, p_to, (real_t)p_weight);
}

// « position:x » désigne une composante d'une propriété.
static bool _split_property(const String &p_property, String &r_base, String &r_component) {
	const size_t colon = p_property.find(':');
	if (colon == String::npos) {
		r_base = p_property;
		r_component = String();
		return false;
	}
	r_base = p_property.substr(0, colon);
	r_component = p_property.substr(colon + 1);
	return true;
}

Variant Tween::get_indexed(Object *p_object, const String &p_property, bool *r_valid) {
	String base, component;
	const bool indexed = _split_property(p_property, base, component);
	bool valid = false;
	const Variant value = p_object->get(base, &valid);
	if (r_valid) {
		*r_valid = valid;
	}
	if (!indexed || !valid) {
		return value;
	}
	const char c = component.empty() ? '?' : component[0];
	switch (value.get_type()) {
		case Variant::VECTOR2: {
			const Vector2 v = value.as_vector2();
			return Variant((double)(c == 'x' ? v.x : v.y));
		}
		case Variant::VECTOR3: {
			const Vector3 v = value.as_vector3();
			return Variant((double)(c == 'x' ? v.x : (c == 'y' ? v.y : v.z)));
		}
		case Variant::COLOR: {
			const Color v = value.as_color();
			return Variant((double)(c == 'r' ? v.r : (c == 'g' ? v.g : (c == 'b' ? v.b : v.a))));
		}
		default:
			if (r_valid) {
				*r_valid = false;
			}
			return Variant();
	}
}

void Tween::set_indexed(Object *p_object, const String &p_property, const Variant &p_value) {
	String base, component;
	if (!_split_property(p_property, base, component)) {
		p_object->set(base, p_value);
		return;
	}
	Variant value = p_object->get(base);
	const char c = component.empty() ? '?' : component[0];
	const real_t f = (real_t)p_value.as_float();
	switch (value.get_type()) {
		case Variant::VECTOR2: {
			Vector2 v = value.as_vector2();
			(c == 'x' ? v.x : v.y) = f;
			value = Variant(v);
		} break;
		case Variant::VECTOR3: {
			Vector3 v = value.as_vector3();
			(c == 'x' ? v.x : (c == 'y' ? v.y : v.z)) = f;
			value = Variant(v);
		} break;
		case Variant::COLOR: {
			Color v = value.as_color();
			(c == 'r' ? v.r : (c == 'g' ? v.g : (c == 'b' ? v.b : v.a))) = f;
			value = Variant(v);
		} break;
		default:
			return;
	}
	p_object->set(base, value);
}

// a + b pour les types animables (étapes relatives).
static Variant _add_values(const Variant &p_a, const Variant &p_b) {
	switch (p_a.get_type()) {
		case Variant::INT:
			return Variant((int64_t)(p_a.as_int() + p_b.as_int()));
		case Variant::FLOAT:
			return Variant(p_a.as_float() + p_b.as_float());
		case Variant::VECTOR2:
			return Variant(p_a.as_vector2() + p_b.as_vector2());
		case Variant::VECTOR3:
			return Variant(p_a.as_vector3() + p_b.as_vector3());
		case Variant::COLOR: {
			const Color a = p_a.as_color(), b = p_b.as_color();
			return Variant(Color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a));
		}
		default:
			return p_b;
	}
}

// ---------------------------------------------------------------------------
// Étapes
// ---------------------------------------------------------------------------
void PropertyTweener::start() {
	Tweener::start();
	began = false;
}

double PropertyTweener::step(double p_delta, Tween *p_tween) {
	if (finished) {
		return p_delta;
	}
	Object *object = ObjectDB::get_instance(target);
	if (!object) {
		finished = true; // la cible a été supprimée : l'étape est abandonnée
		return p_delta;
	}
	elapsed += p_delta;
	if (elapsed < delay) {
		return -1;
	}
	if (!began) {
		// La valeur de départ est lue au moment où l'étape commence réellement.
		began = true;
		begin = has_from && !from_current ? initial : Tween::get_indexed(object, property);
		end = relative ? _add_values(begin, final_value) : final_value;
		// Un entier animé vers un flottant (ou l'inverse) : même type des deux côtés.
		if (begin.get_type() == Variant::INT && end.get_type() == Variant::FLOAT) {
			begin = Variant(begin.as_float());
		} else if (begin.get_type() == Variant::FLOAT && end.get_type() == Variant::INT) {
			end = Variant(end.as_float());
		}
	}
	const double time = elapsed - delay;
	if (time < duration) {
		const double weight = Tween::run_equation(trans >= 0 ? trans : p_tween->get_default_trans(), ease >= 0 ? ease : p_tween->get_default_ease(), time / duration);
		Tween::set_indexed(object, property, Tween::interpolate_value(begin, end, weight));
		return -1;
	}
	Tween::set_indexed(object, property, end);
	finished = true;
	return time - duration;
}

Ref<PropertyTweener> PropertyTweener::from(const Variant &p_value) {
	initial = p_value;
	has_from = true;
	from_current = false;
	return Ref<PropertyTweener>(this);
}

Ref<PropertyTweener> PropertyTweener::from_current_value() {
	// La valeur actuelle est figée dès maintenant (et non au lancement de l'étape).
	if (Object *object = ObjectDB::get_instance(target)) {
		initial = Tween::get_indexed(object, property);
		has_from = true;
	}
	return Ref<PropertyTweener>(this);
}

Ref<PropertyTweener> PropertyTweener::as_relative() {
	relative = true;
	return Ref<PropertyTweener>(this);
}

Ref<PropertyTweener> PropertyTweener::set_trans(int p_trans) {
	trans = p_trans;
	return Ref<PropertyTweener>(this);
}

Ref<PropertyTweener> PropertyTweener::set_ease(int p_ease) {
	ease = p_ease;
	return Ref<PropertyTweener>(this);
}

Ref<PropertyTweener> PropertyTweener::set_delay(double p_delay) {
	delay = std::max(0.0, p_delay);
	return Ref<PropertyTweener>(this);
}

void PropertyTweener::_bind_methods() {
	ClassDB::bind_method(D_METHOD("from", "value"), &PropertyTweener::from);
	ClassDB::bind_method(D_METHOD("from_current"), &PropertyTweener::from_current_value);
	ClassDB::bind_method(D_METHOD("as_relative"), &PropertyTweener::as_relative);
	ClassDB::bind_method(D_METHOD("set_trans", "trans"), &PropertyTweener::set_trans);
	ClassDB::bind_method(D_METHOD("set_ease", "ease"), &PropertyTweener::set_ease);
	ClassDB::bind_method(D_METHOD("set_delay", "delay"), &PropertyTweener::set_delay);
}

double IntervalTweener::step(double p_delta, Tween *p_tween) {
	if (finished) {
		return p_delta;
	}
	elapsed += p_delta;
	if (elapsed < duration) {
		return -1;
	}
	finished = true;
	return elapsed - duration;
}

double CallbackTweener::step(double p_delta, Tween *p_tween) {
	if (finished) {
		return p_delta;
	}
	elapsed += p_delta;
	if (elapsed < delay) {
		return -1;
	}
	finished = true;
	const double rest = elapsed - delay;
	if (callback.is_valid()) {
		CallError error;
		callback.callp(nullptr, 0, error);
	}
	return rest;
}

Ref<CallbackTweener> CallbackTweener::set_delay(double p_delay) {
	delay = std::max(0.0, p_delay);
	return Ref<CallbackTweener>(this);
}

void CallbackTweener::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_delay", "delay"), &CallbackTweener::set_delay);
}

double MethodTweener::step(double p_delta, Tween *p_tween) {
	if (finished) {
		return p_delta;
	}
	elapsed += p_delta;
	if (elapsed < delay) {
		return -1;
	}
	const double time = elapsed - delay;
	const bool done = time >= duration;
	const double weight = done ? 1.0 : Tween::run_equation(trans >= 0 ? trans : p_tween->get_default_trans(), ease >= 0 ? ease : p_tween->get_default_ease(), time / duration);
	finished = done;
	if (callback.is_valid()) {
		const Variant value = done ? to_value : Tween::interpolate_value(from_value, to_value, weight);
		const Variant *args[1] = { &value };
		CallError error;
		callback.callp(args, 1, error);
	}
	return done ? time - duration : -1;
}

Ref<MethodTweener> MethodTweener::set_trans(int p_trans) {
	trans = p_trans;
	return Ref<MethodTweener>(this);
}

Ref<MethodTweener> MethodTweener::set_ease(int p_ease) {
	ease = p_ease;
	return Ref<MethodTweener>(this);
}

Ref<MethodTweener> MethodTweener::set_delay(double p_delay) {
	delay = std::max(0.0, p_delay);
	return Ref<MethodTweener>(this);
}

void MethodTweener::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_trans", "trans"), &MethodTweener::set_trans);
	ClassDB::bind_method(D_METHOD("set_ease", "ease"), &MethodTweener::set_ease);
	ClassDB::bind_method(D_METHOD("set_delay", "delay"), &MethodTweener::set_delay);
}

// ---------------------------------------------------------------------------
// Tween
// ---------------------------------------------------------------------------
void Tween::_append(const Ref<Tweener> &p_tweener) {
	if (started) {
		print_error("Tween : impossible d'ajouter une étape à un tween déjà lancé.");
		return;
	}
	if ((parallel_next || default_parallel) && !steps.empty()) {
		steps.back().push_back(p_tweener);
	} else {
		steps.push_back({ p_tweener });
	}
	parallel_next = false;
}

Ref<PropertyTweener> Tween::tween_property(Object *p_object, const String &p_property, const Variant &p_final, double p_duration) {
	Ref<PropertyTweener> tweener;
	ERR_FAIL_COND_V_MSG(!p_object, tweener, "Tween : objet nul.");
	bool valid = false;
	get_indexed(p_object, p_property, &valid);
	ERR_FAIL_COND_V_MSG(!valid, tweener, "Tween : propriété inconnue '" + p_property + "'.");
	tweener.instantiate();
	tweener->target = p_object->get_instance_id();
	tweener->property = p_property;
	tweener->final_value = p_final;
	tweener->duration = std::max(0.0, p_duration);
	_append(tweener);
	return tweener;
}

Ref<IntervalTweener> Tween::tween_interval(double p_time) {
	Ref<IntervalTweener> tweener;
	tweener.instantiate();
	tweener->duration = std::max(0.0, p_time);
	_append(tweener);
	return tweener;
}

Ref<CallbackTweener> Tween::tween_callback(const Callable &p_callback) {
	Ref<CallbackTweener> tweener;
	tweener.instantiate();
	tweener->callback = p_callback;
	_append(tweener);
	return tweener;
}

Ref<MethodTweener> Tween::tween_method(const Callable &p_callback, const Variant &p_from, const Variant &p_to, double p_duration) {
	Ref<MethodTweener> tweener;
	tweener.instantiate();
	tweener->callback = p_callback;
	tweener->from_value = p_from;
	tweener->to_value = p_to;
	tweener->duration = std::max(0.0, p_duration);
	_append(tweener);
	return tweener;
}

Ref<Tween> Tween::parallel() {
	parallel_next = true;
	return Ref<Tween>(this);
}

Ref<Tween> Tween::chain() {
	parallel_next = false;
	if (default_parallel && !steps.empty()) {
		steps.push_back({}); // l'étape suivante ouvre un nouveau groupe
	}
	return Ref<Tween>(this);
}

Ref<Tween> Tween::set_parallel(bool p_parallel) {
	default_parallel = p_parallel;
	return Ref<Tween>(this);
}

Ref<Tween> Tween::set_loops(int p_loops) {
	loops = std::max(0, p_loops); // 0 : sans fin
	return Ref<Tween>(this);
}

Ref<Tween> Tween::set_trans(int p_trans) {
	default_trans = Math::clamp(p_trans, 0, (int)TRANS_MAX - 1);
	return Ref<Tween>(this);
}

Ref<Tween> Tween::set_ease(int p_ease) {
	default_ease = Math::clamp(p_ease, 0, (int)EASE_MAX - 1);
	return Ref<Tween>(this);
}

Ref<Tween> Tween::set_speed_scale(double p_scale) {
	speed_scale = std::max(0.0, p_scale);
	return Ref<Tween>(this);
}

Ref<Tween> Tween::bind_node(Node *p_node) {
	bound = p_node != nullptr;
	bound_node = p_node ? p_node->get_instance_id() : 0;
	return Ref<Tween>(this);
}

void Tween::kill() {
	dead = true;
	running = false;
}

void Tween::stop() {
	running = false;
	started = false;
	current_step = 0;
	loops_done = 0;
	total_time = 0;
}

void Tween::_start_step() {
	for (Ref<Tweener> &tweener : steps[(size_t)current_step]) {
		tweener->start();
	}
}

bool Tween::custom_step(double p_delta) {
	if (dead) {
		return false;
	}
	if (bound) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(bound_node));
		if (!node) {
			kill(); // le nœud lié n'existe plus
			return false;
		}
		if (!node->is_inside_tree()) {
			return true; // en attente : le nœud est hors de l'arbre
		}
	}
	// Les groupes vides (chain() sans suite) sont ignorés.
	steps.erase(std::remove_if(steps.begin(), steps.end(), [](const std::vector<Ref<Tweener>> &p_group) { return p_group.empty(); }), steps.end());
	if (steps.empty()) {
		kill();
		return false;
	}
	if (!started) {
		started = true;
		current_step = 0;
		_start_step();
	}
	Ref<Tween> keep_alive(this); // un rappel peut relâcher la dernière référence
	double remaining = p_delta * speed_scale;
	total_time += remaining;
	int guard = 0;
	while (remaining >= 0 && !dead) {
		double step_rest = remaining;
		bool step_done = true;
		// Copie : un rappel pourrait modifier la liste.
		const std::vector<Ref<Tweener>> group = steps[(size_t)current_step];
		for (const Ref<Tweener> &tweener : group) {
			const double rest = const_cast<Tweener *>(tweener.ptr())->step(remaining, this);
			if (rest < 0) {
				step_done = false;
			} else {
				step_rest = std::min(step_rest, rest);
			}
		}
		if (!step_done || dead) {
			break;
		}
		emit_signal("step_finished", current_step);
		if (dead) {
			break;
		}
		remaining = step_rest;
		current_step++;
		if (current_step >= (int)steps.size()) {
			loops_done++;
			if (loops > 0 && loops_done >= loops) {
				running = false;
				dead = true;
				emit_signal("finished");
				return false;
			}
			emit_signal("loop_finished", loops_done);
			current_step = 0;
			// Une boucle de durée nulle tournerait sans fin dans la même image.
			if (remaining >= p_delta * speed_scale || ++guard > 64) {
				_start_step();
				break;
			}
		}
		_start_step();
	}
	return !dead;
}

void Tween::_bind_methods() {
	ClassDB::bind_method(D_METHOD("tween_property", "object", "property", "final_value", "duration"), &Tween::tween_property);
	ClassDB::bind_method(D_METHOD("tween_interval", "time"), &Tween::tween_interval);
	ClassDB::bind_method(D_METHOD("tween_callback", "callback"), &Tween::tween_callback);
	ClassDB::bind_method(D_METHOD("tween_method", "method", "from", "to", "duration"), &Tween::tween_method);
	ClassDB::bind_method(D_METHOD("parallel"), &Tween::parallel);
	ClassDB::bind_method(D_METHOD("chain"), &Tween::chain);
	ClassDB::bind_method(D_METHOD("set_parallel", "parallel"), &Tween::set_parallel, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("set_loops", "loops"), &Tween::set_loops, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("set_trans", "trans"), &Tween::set_trans);
	ClassDB::bind_method(D_METHOD("set_ease", "ease"), &Tween::set_ease);
	ClassDB::bind_method(D_METHOD("set_speed_scale", "speed"), &Tween::set_speed_scale);
	ClassDB::bind_method(D_METHOD("bind_node", "node"), &Tween::bind_node);
	ClassDB::bind_method(D_METHOD("kill"), &Tween::kill);
	ClassDB::bind_method(D_METHOD("stop"), &Tween::stop);
	ClassDB::bind_method(D_METHOD("pause"), &Tween::pause);
	ClassDB::bind_method(D_METHOD("play"), &Tween::play);
	ClassDB::bind_method(D_METHOD("is_running"), &Tween::is_running);
	ClassDB::bind_method(D_METHOD("is_valid"), &Tween::is_valid);
	ClassDB::bind_method(D_METHOD("get_total_elapsed_time"), &Tween::get_total_elapsed_time);
	ClassDB::bind_method(D_METHOD("get_loops_left"), &Tween::get_loops_left);
	ClassDB::bind_method(D_METHOD("custom_step", "delta"), &Tween::custom_step);
	ClassDB::bind_method(D_METHOD("interpolate_value", "from", "to", "t", "trans", "ease"), &Tween::_interpolate_bind);
	ADD_SIGNAL(MethodInfo("finished"));
	ADD_SIGNAL(MethodInfo("step_finished", PropertyInfo(Variant::INT, "index")));
	ADD_SIGNAL(MethodInfo("loop_finished", PropertyInfo(Variant::INT, "loop_count")));
	BIND_ENUM_CONSTANT(TRANS_LINEAR);
	BIND_ENUM_CONSTANT(TRANS_SINE);
	BIND_ENUM_CONSTANT(TRANS_QUINT);
	BIND_ENUM_CONSTANT(TRANS_QUART);
	BIND_ENUM_CONSTANT(TRANS_QUAD);
	BIND_ENUM_CONSTANT(TRANS_EXPO);
	BIND_ENUM_CONSTANT(TRANS_ELASTIC);
	BIND_ENUM_CONSTANT(TRANS_CUBIC);
	BIND_ENUM_CONSTANT(TRANS_CIRC);
	BIND_ENUM_CONSTANT(TRANS_BOUNCE);
	BIND_ENUM_CONSTANT(TRANS_BACK);
	BIND_ENUM_CONSTANT(EASE_IN);
	BIND_ENUM_CONSTANT(EASE_OUT);
	BIND_ENUM_CONSTANT(EASE_IN_OUT);
	BIND_ENUM_CONSTANT(EASE_OUT_IN);
}
