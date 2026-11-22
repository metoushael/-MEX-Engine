// InputMap (actions -> touches) et Input (état courant des entrées).
#pragma once

#include <functional>

#include "core/input/input_event.h"

class InputMap : public Object {
	GDCLASS(InputMap, Object)

	static InputMap *singleton;
	std::map<String, std::vector<int>> actions;

protected:
	static void _bind_methods();

public:
	static InputMap *get_singleton() { return singleton; }

	void add_action(const String &p_action);
	void erase_action(const String &p_action);
	bool has_action(const String &p_action) const { return actions.count(p_action) > 0; }
	void action_add_code(const String &p_action, int p_code);
	void action_erase_codes(const String &p_action);
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
		uint64_t pressed_process_frame = UINT64_MAX;
		uint64_t pressed_physics_frame = UINT64_MAX;
		uint64_t released_process_frame = UINT64_MAX;
		uint64_t released_physics_frame = UINT64_MAX;
	};

	std::set<int> codes_pressed; // touches + boutons de souris (avec drapeau)
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
	float get_axis(const String &p_negative, const String &p_positive) const;
	Vector2 get_vector(const String &p_neg_x, const String &p_pos_x, const String &p_neg_y, const String &p_pos_y) const;
	Vector2 get_mouse_position() const { return mouse_position; }
	void set_mouse_position(const Vector2 &p_pos) { mouse_position = p_pos; }

	// Simulation d'actions depuis le code.
	void action_press(const String &p_action);
	void action_release(const String &p_action);

	Input();
	~Input();
};
