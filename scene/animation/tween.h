// Tween : anime des propriétés par code, sans préparer d'animation.
//
//   var t = create_tween()
//   t.tween_property(sprite, "position", Vector2(300, 100), 0.5).set_trans(Tween.TRANS_BACK)
//   t.tween_interval(0.2)
//   t.tween_callback(sprite.queue_free)
//
// Les étapes s'enchaînent ; parallel() fait démarrer l'étape suivante avec la précédente.
#pragma once

#include "core/object/ref_counted.h"

class Node;
class Tween;

class Tweener : public RefCounted {
	GDCLASS(Tweener, RefCounted)

protected:
	friend class Tween;
	double elapsed = 0;
	bool finished = false;
	int trans = -1; // -1 : réglage du Tween
	int ease = -1;
	double delay = 0;

	static void _bind_methods() {}
	virtual void start() {
		elapsed = 0;
		finished = false;
	}
	// Avance de p_delta ; renvoie le temps non utilisé (étape terminée) ou -1 (encore en cours).
	virtual double step(double p_delta, Tween *p_tween) = 0;
};

class PropertyTweener : public Tweener {
	GDCLASS(PropertyTweener, Tweener)

	friend class Tween;
	ObjectID target = 0;
	String property;
	Variant initial;
	Variant final_value;
	Variant delta_value;
	double duration = 0;
	bool has_from = false;
	bool relative = false;
	bool from_current = false;
	Variant begin; // valeur de départ fixée au lancement de l'étape
	Variant end;
	bool began = false;

protected:
	static void _bind_methods();
	virtual void start() override;
	virtual double step(double p_delta, Tween *p_tween) override;

public:
	Ref<PropertyTweener> from(const Variant &p_value);
	Ref<PropertyTweener> from_current_value();
	Ref<PropertyTweener> as_relative();
	Ref<PropertyTweener> set_trans(int p_trans);
	Ref<PropertyTweener> set_ease(int p_ease);
	Ref<PropertyTweener> set_delay(double p_delay);
};

class IntervalTweener : public Tweener {
	GDCLASS(IntervalTweener, Tweener)

	friend class Tween;
	double duration = 0;

protected:
	static void _bind_methods() {}
	virtual double step(double p_delta, Tween *p_tween) override;
};

class CallbackTweener : public Tweener {
	GDCLASS(CallbackTweener, Tweener)

	friend class Tween;
	Callable callback;

protected:
	static void _bind_methods();
	virtual double step(double p_delta, Tween *p_tween) override;

public:
	Ref<CallbackTweener> set_delay(double p_delay);
};

class MethodTweener : public Tweener {
	GDCLASS(MethodTweener, Tweener)

	friend class Tween;
	Callable callback;
	Variant from_value;
	Variant to_value;
	double duration = 0;

protected:
	static void _bind_methods();
	virtual double step(double p_delta, Tween *p_tween) override;

public:
	Ref<MethodTweener> set_trans(int p_trans);
	Ref<MethodTweener> set_ease(int p_ease);
	Ref<MethodTweener> set_delay(double p_delay);
};

class Tween : public RefCounted {
	GDCLASS(Tween, RefCounted)

public:
	enum TransitionType {
		TRANS_LINEAR,
		TRANS_SINE,
		TRANS_QUINT,
		TRANS_QUART,
		TRANS_QUAD,
		TRANS_EXPO,
		TRANS_ELASTIC,
		TRANS_CUBIC,
		TRANS_CIRC,
		TRANS_BOUNCE,
		TRANS_BACK,
		TRANS_MAX
	};
	enum EaseType {
		EASE_IN,
		EASE_OUT,
		EASE_IN_OUT,
		EASE_OUT_IN,
		EASE_MAX
	};

private:
	std::vector<std::vector<Ref<Tweener>>> steps;
	int current_step = 0;
	int loops = 1;
	int loops_done = 0;
	int default_trans = TRANS_LINEAR;
	int default_ease = EASE_IN_OUT;
	double speed_scale = 1.0;
	bool parallel_next = false;
	bool default_parallel = false;
	bool running = true;
	bool started = false;
	bool dead = false;
	bool valid = true;
	bool bound = false;
	ObjectID bound_node = 0;
	double total_time = 0;

	void _append(const Ref<Tweener> &p_tweener);
	Variant _interpolate_bind(const Variant &p_from, const Variant &p_to, double p_t, int p_trans, int p_ease) const {
		return interpolate_value(p_from, p_to, run_equation(p_trans, p_ease, p_t));
	}
	void _start_step();

protected:
	static void _bind_methods();

public:
	Ref<PropertyTweener> tween_property(Object *p_object, const String &p_property, const Variant &p_final, double p_duration);
	Ref<IntervalTweener> tween_interval(double p_time);
	Ref<CallbackTweener> tween_callback(const Callable &p_callback);
	Ref<MethodTweener> tween_method(const Callable &p_callback, const Variant &p_from, const Variant &p_to, double p_duration);

	Ref<Tween> parallel();
	Ref<Tween> chain();
	Ref<Tween> set_parallel(bool p_parallel);
	Ref<Tween> set_loops(int p_loops);
	Ref<Tween> set_trans(int p_trans);
	Ref<Tween> set_ease(int p_ease);
	Ref<Tween> set_speed_scale(double p_scale);
	Ref<Tween> bind_node(Node *p_node);
	int get_default_trans() const { return default_trans; }
	int get_default_ease() const { return default_ease; }

	void kill();
	void stop();
	void pause() { running = false; }
	void play() { running = !dead; }
	bool is_running() const { return running && !dead; }
	bool is_valid() const { return valid && !dead; }
	bool is_dead() const { return dead; }
	double get_total_elapsed_time() const { return total_time; }
	int get_loops_left() const { return loops <= 0 ? -1 : std::max(0, loops - loops_done); }

	// Avance le tween ; renvoie faux quand il est terminé (il peut alors être oublié).
	bool custom_step(double p_delta);

	// Courbe d'accélération : p_t entre 0 et 1, résultat entre 0 et 1 (peut dépasser pour BACK et ELASTIC).
	static double run_equation(int p_trans, int p_ease, double p_t);
	static Variant interpolate_value(const Variant &p_from, const Variant &p_to, double p_weight);
	static Variant get_indexed(Object *p_object, const String &p_property, bool *r_valid = nullptr);
	static void set_indexed(Object *p_object, const String &p_property, const Variant &p_value);
};
