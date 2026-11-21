// AnimatedSprite2D : Sprite2D dont les images d'une planche (hframes x vframes) sont jouées
// en séquence. Chaque animation est une plage d'images : [première, dernière, images/s, boucle].
#pragma once

#include "scene/2d/sprite_2d.h"

class AnimatedSprite2D : public Sprite2D {
	GDCLASS(AnimatedSprite2D, Sprite2D)

	Dictionary animations; // nom -> [première image, dernière image, images/s, boucle]
	String animation;
	String autoplay;
	real_t speed_scale = 1.0f;
	bool playing = false;
	double frame_time = 0;

	bool _get_range(const String &p_name, int &r_first, int &r_last, real_t &r_fps, bool &r_loop) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_animations(const Dictionary &p_animations);
	Dictionary get_animations() const { return animations; }
	void set_animation(const String &p_name);
	String get_animation() const { return animation; }
	void set_autoplay(const String &p_name) { autoplay = p_name; }
	String get_autoplay() const { return autoplay; }
	void set_speed_scale(real_t p_scale) { speed_scale = p_scale; }
	real_t get_speed_scale() const { return speed_scale; }

	void add_animation(const String &p_name, int p_first, int p_last, real_t p_fps, bool p_loop);
	void remove_animation(const String &p_name);
	bool has_animation(const String &p_name) const { return animations.has(Variant(p_name)); }
	Array get_animation_list() const { return animations.keys(); }

	void play(const String &p_name = "");
	void stop();
	void pause() { playing = false; }
	bool is_playing() const { return playing; }
	// Avance l'animation (appelé chaque image en jeu ; l'éditeur s'en sert pour l'aperçu).
	void advance(double p_delta);
};
