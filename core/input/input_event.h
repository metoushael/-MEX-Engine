// Événements d'entrée (clavier, souris).
#pragma once

#include "core/input/keyboard.h"
#include "core/object/ref_counted.h"

class InputEvent : public RefCounted {
	GDCLASS(InputEvent, RefCounted)

protected:
	static void _bind_methods();

public:
	virtual bool is_pressed() const { return false; }
	virtual bool is_echo() const { return false; }
	virtual int get_input_code() const { return 0; } // code comparable à l'InputMap
	virtual String as_text() const { return "InputEvent"; }

	bool is_action(const String &p_action) const;
	bool is_action_pressed(const String &p_action, bool p_allow_echo = false) const;
	bool is_action_released(const String &p_action) const;
};

class InputEventKey : public InputEvent {
	GDCLASS(InputEventKey, InputEvent)

	int keycode = 0;
	bool pressed = false;
	bool echo = false;

	String text;

protected:
	static void _bind_methods();

public:
	// Texte saisi (événement de saisie de texte : keycode nul, déjà composé par le système).
	void set_text(const String &p_text) { text = p_text; }
	String get_text() const { return text; }
	void set_keycode(int p_keycode) { keycode = p_keycode; }
	int get_keycode() const { return keycode; }
	void set_pressed(bool p_pressed) { pressed = p_pressed; }
	void set_echo(bool p_echo) { echo = p_echo; }
	virtual bool is_pressed() const override { return pressed; }
	virtual bool is_echo() const override { return echo; }
	virtual int get_input_code() const override { return keycode; }
	virtual String as_text() const override;
};

class InputEventMouse : public InputEvent {
	GDCLASS(InputEventMouse, InputEvent)

	Vector2 position;

protected:
	static void _bind_methods();

public:
	void set_position(const Vector2 &p_pos) { position = p_pos; }
	Vector2 get_position() const { return position; }
};

class InputEventMouseButton : public InputEventMouse {
	GDCLASS(InputEventMouseButton, InputEventMouse)

	int button_index = 0;
	bool pressed = false;
	bool double_click = false;

protected:
	static void _bind_methods();

public:
	void set_button_index(int p_index) { button_index = p_index; }
	int get_button_index() const { return button_index; }
	void set_pressed(bool p_pressed) { pressed = p_pressed; }
	void set_double_click(bool p_double) { double_click = p_double; }
	bool is_double_click() const { return double_click; }
	virtual bool is_pressed() const override { return pressed; }
	virtual int get_input_code() const override { return INPUT_CODE_MOUSE_FLAG | button_index; }
	virtual String as_text() const override;
};

// Bouton de manette (classe et propriétés de Godot).
class InputEventJoypadButton : public InputEvent {
	GDCLASS(InputEventJoypadButton, InputEvent)

	int device = 0;
	int button_index = 0;
	bool pressed = false;
	float pressure = 0.0f;

protected:
	static void _bind_methods();

public:
	void set_device(int p_device) { device = p_device; }
	int get_device() const { return device; }
	void set_button_index(int p_index) { button_index = p_index; }
	int get_button_index() const { return button_index; }
	void set_pressed(bool p_pressed) { pressed = p_pressed; }
	void set_pressure(float p_pressure) { pressure = p_pressure; }
	float get_pressure() const { return pressure; }
	virtual bool is_pressed() const override { return pressed; }
	virtual int get_input_code() const override { return input_code_joy_button(button_index); }
	virtual String as_text() const override;
};

// Déplacement d'un axe de manette : -1 à 1 pour les sticks, 0 à 1 pour les gâchettes.
class InputEventJoypadMotion : public InputEvent {
	GDCLASS(InputEventJoypadMotion, InputEvent)

	int device = 0;
	int axis = 0;
	float axis_value = 0.0f;

protected:
	static void _bind_methods();

public:
	void set_device(int p_device) { device = p_device; }
	int get_device() const { return device; }
	void set_axis(int p_axis) { axis = p_axis; }
	int get_axis() const { return axis; }
	void set_axis_value(float p_value) { axis_value = p_value; }
	float get_axis_value() const { return axis_value; }
	virtual String as_text() const override;
};

class InputEventMouseMotion : public InputEventMouse {
	GDCLASS(InputEventMouseMotion, InputEventMouse)

	Vector2 relative;

protected:
	static void _bind_methods();

public:
	void set_relative(const Vector2 &p_rel) { relative = p_rel; }
	Vector2 get_relative() const { return relative; }
	virtual String as_text() const override { return "Mouvement de souris"; }
};
