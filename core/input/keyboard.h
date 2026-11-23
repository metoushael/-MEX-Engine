// Codes de touches (compatibles avec les keycodes SDL) et boutons de souris.
#pragma once

#include "core/typedefs.h"

enum Key : int {
	KEY_NONE = 0,
	KEY_BACKSPACE = 8,
	KEY_TAB = 9,
	KEY_ENTER = 13,
	KEY_ESCAPE = 27,
	KEY_SPACE = 32,
	KEY_0 = 48,
	KEY_9 = 57,
	KEY_A = 97,
	KEY_Z = 122,
	KEY_DELETE = 127,
	KEY_SPECIAL = 1 << 30,
	KEY_F1 = KEY_SPECIAL | 58,
	KEY_F12 = KEY_SPECIAL | 69,
	KEY_RIGHT = KEY_SPECIAL | 79,
	KEY_LEFT = KEY_SPECIAL | 80,
	KEY_DOWN = KEY_SPECIAL | 81,
	KEY_UP = KEY_SPECIAL | 82,
	KEY_CTRL = KEY_SPECIAL | 224,
	KEY_SHIFT = KEY_SPECIAL | 225,
	KEY_ALT = KEY_SPECIAL | 226,
};

enum MouseButton : int {
	MOUSE_BUTTON_NONE = 0,
	MOUSE_BUTTON_LEFT = 1,
	MOUSE_BUTTON_RIGHT = 2,
	MOUSE_BUTTON_MIDDLE = 3,
	MOUSE_BUTTON_WHEEL_UP = 4,
	MOUSE_BUTTON_WHEEL_DOWN = 5,
};

// Dans l'InputMap, un bouton de souris est encodé avec ce drapeau.
#define INPUT_CODE_MOUSE_FLAG (1 << 29)

// Manette : boutons et axes numérotés comme dans Godot (JoyButton, JoyAxis), qui reprend la
// disposition de SDL_GameController.
enum JoyButton : int {
	JOY_BUTTON_INVALID = -1,
	JOY_BUTTON_A = 0,
	JOY_BUTTON_B = 1,
	JOY_BUTTON_X = 2,
	JOY_BUTTON_Y = 3,
	JOY_BUTTON_BACK = 4,
	JOY_BUTTON_GUIDE = 5,
	JOY_BUTTON_START = 6,
	JOY_BUTTON_LEFT_STICK = 7,
	JOY_BUTTON_RIGHT_STICK = 8,
	JOY_BUTTON_LEFT_SHOULDER = 9,
	JOY_BUTTON_RIGHT_SHOULDER = 10,
	JOY_BUTTON_DPAD_UP = 11,
	JOY_BUTTON_DPAD_DOWN = 12,
	JOY_BUTTON_DPAD_LEFT = 13,
	JOY_BUTTON_DPAD_RIGHT = 14,
	JOY_BUTTON_MAX = 21,
};

enum JoyAxis : int {
	JOY_AXIS_INVALID = -1,
	JOY_AXIS_LEFT_X = 0,
	JOY_AXIS_LEFT_Y = 1,
	JOY_AXIS_RIGHT_X = 2,
	JOY_AXIS_RIGHT_Y = 3,
	JOY_AXIS_TRIGGER_LEFT = 4,
	JOY_AXIS_TRIGGER_RIGHT = 5,
	JOY_AXIS_MAX = 6,
};

// Dans l'InputMap : bouton de manette, ou demi-axe (axe et sens).
#define INPUT_CODE_JOY_BUTTON_FLAG (1 << 28)
#define INPUT_CODE_JOY_AXIS_FLAG (1 << 27)
inline int input_code_joy_button(int p_button) { return INPUT_CODE_JOY_BUTTON_FLAG | p_button; }
inline int input_code_joy_axis(int p_axis, bool p_positive) { return INPUT_CODE_JOY_AXIS_FLAG | (p_axis << 1) | (p_positive ? 1 : 0); }
inline bool input_code_is_joy_axis(int p_code) { return (p_code & INPUT_CODE_JOY_AXIS_FLAG) != 0 && (p_code & (INPUT_CODE_MOUSE_FLAG | INPUT_CODE_JOY_BUTTON_FLAG)) == 0; }

String input_code_get_name(int p_code); // "Space", "A", "MouseLeft"...
int input_code_from_name(const String &p_name); // KEY_NONE si inconnu
void input_code_get_all_names(std::vector<std::pair<String, int>> *r_list); // pour scripts : KEY_*
void input_code_get_joy_names(std::vector<std::pair<String, int>> *r_list); // "JoyA", "JoyLeftX+"...
