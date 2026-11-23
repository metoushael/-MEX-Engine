// InputMap (actions -> touches) et Input (état courant des entrées).
#pragma once

#include <functional>

#include "core/input/input_event.h"

class InputMap : public Object {
	GDCLASS(InputMap, Object)

	static InputMap *singleton;
	std::map<String, std::vector<int>> actions;
	std::map<String, float> deadzones;

protected:
	static void _bind_methods();

public:
	static InputMap *get_singleton() { return singleton; }

	void add_action(const String &p_action);
	void erase_action(const String &p_action);
	bool has_action(const String &p_action) const { return actions.count(p_action) > 0; }
	void action_add_code(const String &p_action, int p_code);
	void action_erase_codes(const String &p_action);
	// Manette : bouton, ou axe avec son sens (-1 ou 1).
	void action_add_joy_button(const String &p_action, int p_button);
	void action_add_joy_axis(const String &p_action, int p_axis, float p_sign);
	// Zone morte : en dessous, un axe ne déclenche pas l'action (0,5 pour les actions ui_*, 0,2 sinon).
	void action_set_deadzone(const String &p_action, float p_deadzone);
	float action_get_deadzone(const String &p_action) const;
	bool action_has_deadzone(const String &p_action) const { return deadzones.count(p_action) > 0; }
	const std::vector<int> *get_action_codes(const String &p_action) const;
	Array get_actions() const;
	bool event_is_action(const InputEvent *p_event, const String &p_action) const;
	const std::map<String, std::vector<int>> &get_action_map() const { return actions; }

	void load_default(); // ui_left, ui_right, ui_up, ui_down, ui_accept, ui_cancel
	void load_from_project_settings(); // défauts + section [input] du projet

	InputMap();
	~InputMap();
};

class Input : public Object {
	GDCLASS(Input, Object)

	static Input *singleton;

	struct ActionState {
		bool pressed = false;
		float strength = 0.0f; // 0 à 1, zone morte retirée
		float raw_strength = 0.0f; // valeur brute de l'axe
		uint64_t pressed_process_frame = UINT64_MAX;
		uint64_t pressed_physics_frame = UINT64_MAX;
		uint64_t released_process_frame = UINT64_MAX;
		uint64_t released_physics_frame = UINT64_MAX;
	};

	std::set<int> codes_pressed; // touches + boutons de souris et de manette (avec drapeau)
	struct Joypad {
		String name;
		String guid;
		std::set<int> buttons;
		float axes[JOY_AXIS_MAX] = { 0, 0, 0, 0, 0, 0 };
	};
	std::map<int, Joypad> joypads;
	std::map<String, float> forced_actions; // action_press : action -> force
	std::map<String, float> virtual_axes; // action_set_axis_value : action -> valeur brute
	bool touchscreen_present = false;
	bool emulate_touch_from_mouse = false;
	bool mouse_touch_down = false;
	std::function<void(int, float, float, float)> joy_vibration_handler;
	std::function<void(int, int, float)> virtual_joy_handler;
	std::map<String, ActionState> action_states;
	Vector2 mouse_position;
	int mouse_mode = 0;
	std::function<void(int)> mouse_mode_handler;

	void _update_actions();
	void _set_action(const String &p_action, bool p_pressed);

protected:
	static void _bind_methods();

public:
	enum MouseMode {
		MOUSE_MODE_VISIBLE,
		MOUSE_MODE_HIDDEN,
		MOUSE_MODE_CAPTURED, // curseur masqué et verrouillé : seuls les déplacements relatifs comptent
	};

	static Input *get_singleton() { return singleton; }

	void set_mouse_mode(int p_mode);
	int get_mouse_mode() const { return mouse_mode; }
	// Fonction appliquant réellement le mode (fournie par la fenêtre du jeu ; absente dans l'éditeur).
	void set_mouse_mode_handler(std::function<void(int)> p_handler) { mouse_mode_handler = p_handler; }

	void parse_input_event(const Ref<InputEvent> &p_event);
	void release_all();

	bool is_key_pressed(int p_keycode) const;
	bool is_mouse_button_pressed(int p_button) const;
	bool is_action_pressed(const String &p_action) const;
	bool is_action_just_pressed(const String &p_action) const;
	bool is_action_just_released(const String &p_action) const;
	float get_action_strength(const String &p_action) const;
	float get_action_raw_strength(const String &p_action) const;
	float get_axis(const String &p_negative, const String &p_positive) const;
	// p_deadzone < 0 : moyenne des zones mortes des quatre actions.
	Vector2 get_vector(const String &p_neg_x, const String &p_pos_x, const String &p_neg_y, const String &p_pos_y, float p_deadzone = -1.0f) const;

	// Manettes
	void joy_connection_changed(int p_device, bool p_connected, const String &p_name = String(), const String &p_guid = String());
	Array get_connected_joypads() const;
	String get_joy_name(int p_device) const;
	String get_joy_guid(int p_device) const;
	bool is_joy_button_pressed(int p_device, int p_button) const;
	float get_joy_axis(int p_device, int p_axis) const;
	// Vibration : moteurs faible et fort de 0 à 1, durée en secondes (0 : jusqu'à stop_joy_vibration).
	void start_joy_vibration(int p_device, float p_weak, float p_strong, float p_duration = 0.0f);
	void stop_joy_vibration(int p_device);
	void set_joy_vibration_handler(std::function<void(int, float, float, float)> p_handler) { joy_vibration_handler = p_handler; }
	// Manette virtuelle des essais (variable d'environnement MEX_VIRTUAL_JOYPAD=1).
	void virtual_joy_button(int p_button, bool p_pressed);
	void virtual_joy_axis(int p_axis, float p_value);
	void virtual_joy_disconnect(); // comme si on la débranchait
	void set_virtual_joy_handler(std::function<void(int, int, float)> p_handler) { virtual_joy_handler = p_handler; }
	Vector2 get_mouse_position() const { return mouse_position; }
	void set_mouse_position(const Vector2 &p_pos) { mouse_position = p_pos; }

	// Simulation d'actions depuis le code.
	// Force de 0 à 1, comme dans Godot : l'action reste enfoncée jusqu'à action_release, même
	// si d'autres touches sont pressées entre-temps (boutons tactiles, stick à l'écran).
	void action_press(const String &p_action, float p_strength = 1.0f);
	// Écran tactile. Un écran est « disponible » s'il existe, ou si la souris en tient lieu
	// (paramètre input_devices/pointing/emulate_touch_from_mouse).
	// Valeur d'axe virtuelle (0 à 1) pour une action : traitée comme le stick d'une manette,
	// zone morte de l'action comprise. 0 la retire. Sert au stick à l'écran (VirtualJoystick).
	void action_set_axis_value(const String &p_action, float p_value);
	void set_touchscreen_present(bool p_present) { touchscreen_present = p_present; }
	bool is_touchscreen_available() const { return touchscreen_present || emulate_touch_from_mouse; }
	void set_emulate_touch_from_mouse(bool p_enable) { emulate_touch_from_mouse = p_enable; }
	bool is_emulating_touch_from_mouse() const { return emulate_touch_from_mouse; }
	// Événement tactile équivalent à un événement de souris (clic gauche, glissement), ou vide.
	Ref<InputEvent> make_touch_from_mouse(const Ref<InputEvent> &p_event);
	void action_release(const String &p_action);

	Input();
	~Input();
};
