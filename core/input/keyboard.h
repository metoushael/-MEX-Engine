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

String input_code_get_name(int p_code); // "Space", "A", "MouseLeft"...
int input_code_from_name(const String &p_name); // KEY_NONE si inconnu
void input_code_get_all_names(std::vector<std::pair<String, int>> *r_list); // pour scripts : KEY_*
