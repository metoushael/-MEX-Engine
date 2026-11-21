// AnimationPlayer : anime dans le temps des propriétés de nœuds à l'aide d'images clés.
//
// Les animations sont stockées dans un dictionnaire :
//   nom -> { "length": secondes, "loop": bool,
//            "tracks": { "Chemin/Du/Noeud:propriete": [t0, v0, t1, v1, ...] } }
// Les chemins sont relatifs au parent de l'AnimationPlayer (« . » désigne ce parent).
// Les nombres, Vector2 et Color sont interpolés linéairement ; les autres valeurs changent
// à l'instant de la clé.
#pragma once

#include "scene/main/node.h"

class AnimationPlayer : public Node {
	GDCLASS(AnimationPlayer, Node)

	Dictionary animations;
	String autoplay;
	real_t speed_scale = 1.0f;
	String current;
	double position = 0;
	bool playing = false;

	static Variant _deep_copy(const Variant &p_value);
	Dictionary _get_animation(const String &p_name) const;
	Array _get_track(const String &p_name, const String &p_track, bool p_create);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_animations(const Dictionary &p_animations);
	Dictionary get_animations() const { return animations; }
	void set_autoplay(const String &p_name) { autoplay = p_name; }
	String get_autoplay() const { return autoplay; }
	void set_speed_scale(real_t p_scale) { speed_scale = p_scale; }
	real_t get_speed_scale() const { return speed_scale; }

	// Édition (éditeur et scripts).
	void add_animation(const String &p_name, real_t p_length, bool p_loop);
	void remove_animation(const String &p_name);
	bool has_animation(const String &p_name) const { return animations.has(Variant(p_name)); }
	Array get_animation_list() const { return animations.keys(); }
	void set_animation_length(const String &p_name, real_t p_length);
	real_t get_animation_length(const String &p_name) const;
	void set_animation_loop(const String &p_name, bool p_loop);
	bool get_animation_loop(const String &p_name) const;
	Array get_track_list(const String &p_name) const;
	void track_insert_key(const String &p_name, const String &p_track, real_t p_time, const Variant &p_value);
	void track_remove_key(const String &p_name, const String &p_track, int p_index);
	void remove_track(const String &p_name, const String &p_track);
	int track_get_key_count(const String &p_name, const String &p_track) const;
	real_t track_get_key_time(const String &p_name, const String &p_track, int p_index) const;
	Variant track_get_key_value(const String &p_name, const String &p_track, int p_index) const;

	// Lecture.
	void play(const String &p_name = "");
	void stop();
	void pause() { playing = false; }
	bool is_playing() const { return playing; }
	String get_current_animation() const { return current; }
	double get_current_animation_position() const { return position; }
	void seek(double p_time);
	// Applique l'animation à l'instant donné sans changer l'état de lecture.
	void apply(const String &p_name, double p_time);
	void advance(double p_delta);

	static Variant interpolate(const Variant &p_a, const Variant &p_b, real_t p_t);
};
