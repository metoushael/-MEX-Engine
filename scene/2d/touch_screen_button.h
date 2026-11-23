// Commandes tactiles : TouchScreenButton (repris de Godot 4.3, scene/2d/touch_screen_button.cpp,
// licence MIT, voir thirdparty/godot/LICENSE.txt) et VirtualJoystick (propre à MEX).
#pragma once

#include "scene/2d/node_2d.h"
#include "scene/resources/texture.h"

class TouchScreenButton : public Node2D {
	GDCLASS(TouchScreenButton, Node2D)

public:
	enum VisibilityMode {
		VISIBILITY_ALWAYS,
		VISIBILITY_TOUCHSCREEN_ONLY,
	};

private:
	Ref<Texture2D> texture_normal;
	Ref<Texture2D> texture_pressed;
	real_t radius = 48; // sans texture : un disque de ce rayon est dessiné et sert de zone tactile
	bool passby_press = false;
	String action;
	int finger_pressed = -1;
	int visibility = VISIBILITY_ALWAYS;

	bool _is_point_inside(const Vector2 &p_point) const;
	void _press(int p_finger);
	void _release(bool p_exiting_tree = false);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual void input(const Ref<InputEvent> &p_event) override;

	void set_texture_normal(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture_normal() const { return texture_normal; }
	void set_texture_pressed(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture_pressed() const { return texture_pressed; }
	void set_radius(real_t p_radius);
	real_t get_radius() const { return radius; }
	void set_action(const String &p_action) { action = p_action; }
	String get_action() const { return action; }
	void set_passby_press(bool p_enable) { passby_press = p_enable; }
	bool is_passby_press_enabled() const { return passby_press; }
	void set_visibility_mode(int p_mode);
	int get_visibility_mode() const { return visibility; }
	bool is_pressed() const { return finger_pressed != -1; }
	Rect2 get_rect() const;
	virtual Rect2 _edit_get_rect() const override { return get_rect(); } // cadre de sélection dans l'éditeur
};

// Stick analogique à l'écran : le doigt posé dans le cercle déplace la manette, et quatre
// actions reçoivent une valeur de 0 à 1, traitée comme le stick d'une vraie manette (zone
// morte de chaque action comprise : Input.get_vector donne le même résultat).
class VirtualJoystick : public Node2D {
	GDCLASS(VirtualJoystick, Node2D)

	real_t radius = 72;
	String action_left = "ui_left";
	String action_right = "ui_right";
	String action_up = "ui_up";
	String action_down = "ui_down";
	int visibility = TouchScreenButton::VISIBILITY_ALWAYS;
	int finger = -1;
	Vector2 output;

	void _set_output(const Vector2 &p_output);
	Vector2 _to_local(const Vector2 &p_screen) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual void input(const Ref<InputEvent> &p_event) override;

	void set_radius(real_t p_radius);
	real_t get_radius() const { return radius; }
	virtual Rect2 _edit_get_rect() const override { return Rect2(-radius, -radius, radius * 2, radius * 2); }
	void set_action_left(const String &p_action) { action_left = p_action; }
	String get_action_left() const { return action_left; }
	void set_action_right(const String &p_action) { action_right = p_action; }
	String get_action_right() const { return action_right; }
	void set_action_up(const String &p_action) { action_up = p_action; }
	String get_action_up() const { return action_up; }
	void set_action_down(const String &p_action) { action_down = p_action; }
	String get_action_down() const { return action_down; }
	void set_visibility_mode(int p_mode);
	int get_visibility_mode() const { return visibility; }
	Vector2 get_output() const { return output; }
	bool is_pressed() const { return finger != -1; }
};
