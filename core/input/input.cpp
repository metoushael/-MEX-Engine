#include "input.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/string/ustring.h"

// ---------------------------------------------------------------------------
// Noms de touches
// ---------------------------------------------------------------------------
static const std::vector<std::pair<String, int>> &_named_codes() {
	static std::vector<std::pair<String, int>> list;
	if (list.empty()) {
		list = {
			{ "Space", KEY_SPACE }, { "Enter", KEY_ENTER }, { "Escape", KEY_ESCAPE }, { "Tab", KEY_TAB },
			{ "Backspace", KEY_BACKSPACE }, { "Delete", KEY_DELETE }, { "Left", KEY_LEFT }, { "Right", KEY_RIGHT },
			{ "Up", KEY_UP }, { "Down", KEY_DOWN }, { "Shift", KEY_SHIFT }, { "Ctrl", KEY_CTRL }, { "Alt", KEY_ALT },
			{ "MouseLeft", INPUT_CODE_MOUSE_FLAG | MOUSE_BUTTON_LEFT }, { "MouseRight", INPUT_CODE_MOUSE_FLAG | MOUSE_BUTTON_RIGHT },
			{ "MouseMiddle", INPUT_CODE_MOUSE_FLAG | MOUSE_BUTTON_MIDDLE },
		};
		for (int i = 0; i < 26; i++) {
			list.push_back({ String(1, (char)('A' + i)), KEY_A + i });
		}
		for (int i = 0; i < 10; i++) {
			list.push_back({ String(1, (char)('0' + i)), KEY_0 + i });
		}
		for (int i = 0; i < 12; i++) {
			list.push_back({ "F" + std::to_string(i + 1), KEY_F1 + i });
		}
	}
	return list;
}

static const std::vector<std::pair<String, int>> &_joy_codes() {
	static std::vector<std::pair<String, int>> list;
	if (list.empty()) {
		const char *buttons[] = { "JoyA", "JoyB", "JoyX", "JoyY", "JoyBack", "JoyGuide", "JoyStart", "JoyLeftStick", "JoyRightStick",
			"JoyLeftShoulder", "JoyRightShoulder", "JoyDpadUp", "JoyDpadDown", "JoyDpadLeft", "JoyDpadRight" };
		for (int i = 0; i < 15; i++) {
			list.push_back({ buttons[i], input_code_joy_button(i) });
		}
		const char *sticks[] = { "JoyLeftX", "JoyLeftY", "JoyRightX", "JoyRightY" };
		for (int i = 0; i < 4; i++) {
			list.push_back({ String(sticks[i]) + "-", input_code_joy_axis(i, false) });
			list.push_back({ String(sticks[i]) + "+", input_code_joy_axis(i, true) });
		}
		list.push_back({ "JoyTriggerLeft", input_code_joy_axis(JOY_AXIS_TRIGGER_LEFT, true) });
		list.push_back({ "JoyTriggerRight", input_code_joy_axis(JOY_AXIS_TRIGGER_RIGHT, true) });
	}
	return list;
}

void input_code_get_joy_names(std::vector<std::pair<String, int>> *r_list) {
	*r_list = _joy_codes();
}

String input_code_get_name(int p_code) {
	for (auto &kv : _named_codes()) {
		if (kv.second == p_code) {
			return kv.first;
		}
	}
	for (auto &kv : _joy_codes()) {
		if (kv.second == p_code) {
			return kv.first;
		}
	}
	return "Key" + std::to_string(p_code);
}

int input_code_from_name(const String &p_name) {
	String lower = str_to_lower(p_name);
	for (auto &kv : _named_codes()) {
		if (str_to_lower(kv.first) == lower) {
			return kv.second;
		}
	}
	for (auto &kv : _joy_codes()) {
		if (str_to_lower(kv.first) == lower) {
			return kv.second;
		}
	}
	if (str_begins_with(lower, "key")) {
		return atoi(lower.c_str() + 3);
	}
	return KEY_NONE;
}

void input_code_get_all_names(std::vector<std::pair<String, int>> *r_list) {
	*r_list = _named_codes();
}

// ---------------------------------------------------------------------------
// InputEvent
// ---------------------------------------------------------------------------
bool InputEvent::is_action(const String &p_action) const {
	return InputMap::get_singleton()->event_is_action(this, p_action);
}

bool InputEvent::is_action_pressed(const String &p_action, bool p_allow_echo) const {
	return is_pressed() && (p_allow_echo || !is_echo()) && is_action(p_action);
}

bool InputEvent::is_action_released(const String &p_action) const {
	return !is_pressed() && is_action(p_action);
}

String InputEventKey::as_text() const {
	return input_code_get_name(keycode);
}

String InputEventMouseButton::as_text() const {
	return input_code_get_name(INPUT_CODE_MOUSE_FLAG | button_index);
}

void InputEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_pressed"), &InputEvent::is_pressed);
	ClassDB::bind_method(D_METHOD("is_echo"), &InputEvent::is_echo);
	ClassDB::bind_method(D_METHOD("as_text"), &InputEvent::as_text);
	ClassDB::bind_method(D_METHOD("is_action", "action"), &InputEvent::is_action);
	ClassDB::bind_method(D_METHOD("is_action_pressed", "action", "allow_echo"), &InputEvent::is_action_pressed, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("is_action_released", "action"), &InputEvent::is_action_released);
}

void InputEventKey::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_keycode", "keycode"), &InputEventKey::set_keycode);
	ClassDB::bind_method(D_METHOD("get_keycode"), &InputEventKey::get_keycode);
	ClassDB::bind_method(D_METHOD("get_text"), &InputEventKey::get_text);
	ClassDB::bind_method(D_METHOD("set_pressed", "pressed"), &InputEventKey::set_pressed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "keycode"), "set_keycode", "get_keycode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pressed"), "set_pressed", "is_pressed");
}

void InputEventMouse::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_position", "position"), &InputEventMouse::set_position);
	ClassDB::bind_method(D_METHOD("get_position"), &InputEventMouse::get_position);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "position"), "set_position", "get_position");
}

void InputEventMouseButton::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_button_index", "index"), &InputEventMouseButton::set_button_index);
	ClassDB::bind_method(D_METHOD("get_button_index"), &InputEventMouseButton::get_button_index);
	ClassDB::bind_method(D_METHOD("set_pressed", "pressed"), &InputEventMouseButton::set_pressed);
	ClassDB::bind_method(D_METHOD("is_double_click"), &InputEventMouseButton::is_double_click);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "button_index"), "set_button_index", "get_button_index");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pressed"), "set_pressed", "is_pressed");
}

void InputEventMouseMotion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_relative", "relative"), &InputEventMouseMotion::set_relative);
	ClassDB::bind_method(D_METHOD("get_relative"), &InputEventMouseMotion::get_relative);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "relative"), "set_relative", "get_relative");
}

String InputEventJoypadButton::as_text() const {
	return "Manette " + std::to_string(device) + " : " + input_code_get_name(input_code_joy_button(button_index));
}

void InputEventJoypadButton::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_device", "device"), &InputEventJoypadButton::set_device);
	ClassDB::bind_method(D_METHOD("get_device"), &InputEventJoypadButton::get_device);
	ClassDB::bind_method(D_METHOD("set_button_index", "index"), &InputEventJoypadButton::set_button_index);
	ClassDB::bind_method(D_METHOD("get_button_index"), &InputEventJoypadButton::get_button_index);
	ClassDB::bind_method(D_METHOD("set_pressed", "pressed"), &InputEventJoypadButton::set_pressed);
	ClassDB::bind_method(D_METHOD("set_pressure", "pressure"), &InputEventJoypadButton::set_pressure);
	ClassDB::bind_method(D_METHOD("get_pressure"), &InputEventJoypadButton::get_pressure);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "device"), "set_device", "get_device");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "button_index"), "set_button_index", "get_button_index");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pressed"), "set_pressed", "is_pressed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pressure"), "set_pressure", "get_pressure");
}

String InputEventJoypadMotion::as_text() const {
	return "Manette " + std::to_string(device) + " : axe " + std::to_string(axis) + " = " + std::to_string(axis_value);
}

void InputEventJoypadMotion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_device", "device"), &InputEventJoypadMotion::set_device);
	ClassDB::bind_method(D_METHOD("get_device"), &InputEventJoypadMotion::get_device);
	ClassDB::bind_method(D_METHOD("set_axis", "axis"), &InputEventJoypadMotion::set_axis);
	ClassDB::bind_method(D_METHOD("get_axis"), &InputEventJoypadMotion::get_axis);
	ClassDB::bind_method(D_METHOD("set_axis_value", "value"), &InputEventJoypadMotion::set_axis_value);
	ClassDB::bind_method(D_METHOD("get_axis_value"), &InputEventJoypadMotion::get_axis_value);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "device"), "set_device", "get_device");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "axis"), "set_axis", "get_axis");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "axis_value"), "set_axis_value", "get_axis_value");
}

// ---------------------------------------------------------------------------
// InputMap
// ---------------------------------------------------------------------------
InputMap *InputMap::singleton = nullptr;

void InputMap::add_action(const String &p_action) {
	if (!actions.count(p_action)) {
		actions[p_action] = std::vector<int>();
	}
}

void InputMap::erase_action(const String &p_action) {
	actions.erase(p_action);
}

void InputMap::action_add_code(const String &p_action, int p_code) {
	add_action(p_action);
	std::vector<int> &codes = actions[p_action];
	if (p_code != KEY_NONE && std::find(codes.begin(), codes.end(), p_code) == codes.end()) {
		codes.push_back(p_code);
	}
}

void InputMap::action_erase_codes(const String &p_action) {
	auto it = actions.find(p_action);
	if (it != actions.end()) {
		it->second.clear();
	}
}

const std::vector<int> *InputMap::get_action_codes(const String &p_action) const {
	auto it = actions.find(p_action);
	return it == actions.end() ? nullptr : &it->second;
}

Array InputMap::get_actions() const {
	Array out;
	for (auto &kv : actions) {
		out.push_back(Variant(kv.first));
	}
	return out;
}

bool InputMap::event_is_action(const InputEvent *p_event, const String &p_action) const {
	auto it = actions.find(p_action);
	if (it == actions.end() || !p_event) {
		return false;
	}
	if (const InputEventJoypadMotion *motion = Object::cast_to<InputEventJoypadMotion>(p_event)) {
		// Un axe déclenche l'action quand il dépasse la zone morte dans le bon sens.
		const float value = motion->get_axis_value();
		const int wanted = input_code_joy_axis(motion->get_axis(), value >= 0);
		return value != 0.0f && std::abs(value) >= action_get_deadzone(p_action) && std::find(it->second.begin(), it->second.end(), wanted) != it->second.end();
	}
	int code = p_event->get_input_code();
	return code != 0 && std::find(it->second.begin(), it->second.end(), code) != it->second.end();
}

void InputMap::action_add_joy_button(const String &p_action, int p_button) {
	if (p_button >= 0 && p_button < JOY_BUTTON_MAX) {
		action_add_code(p_action, input_code_joy_button(p_button));
	}
}

void InputMap::action_add_joy_axis(const String &p_action, int p_axis, float p_sign) {
	if (p_axis >= 0 && p_axis < JOY_AXIS_MAX) {
		action_add_code(p_action, input_code_joy_axis(p_axis, p_sign >= 0));
	}
}

void InputMap::action_set_deadzone(const String &p_action, float p_deadzone) {
	deadzones[p_action] = std::min(std::max(p_deadzone, 0.0f), 1.0f);
}

float InputMap::action_get_deadzone(const String &p_action) const {
	const auto it = deadzones.find(p_action);
	if (it != deadzones.end()) {
		return it->second;
	}
	return str_begins_with(p_action, "ui_") ? 0.5f : 0.2f;
}

void InputMap::load_default() {
	actions.clear();
	deadzones.clear();
	action_add_code("ui_left", KEY_LEFT);
	action_add_code("ui_right", KEY_RIGHT);
	action_add_code("ui_up", KEY_UP);
	action_add_code("ui_down", KEY_DOWN);
	action_add_code("ui_accept", KEY_ENTER);
	action_add_code("ui_accept", KEY_SPACE);
	action_add_code("ui_cancel", KEY_ESCAPE);
	// Manette, comme les actions par défaut de Godot : croix et stick gauche, A pour valider, B pour annuler.
	action_add_joy_button("ui_left", JOY_BUTTON_DPAD_LEFT);
	action_add_joy_button("ui_right", JOY_BUTTON_DPAD_RIGHT);
	action_add_joy_button("ui_up", JOY_BUTTON_DPAD_UP);
	action_add_joy_button("ui_down", JOY_BUTTON_DPAD_DOWN);
	action_add_joy_axis("ui_left", JOY_AXIS_LEFT_X, -1);
	action_add_joy_axis("ui_right", JOY_AXIS_LEFT_X, 1);
	action_add_joy_axis("ui_up", JOY_AXIS_LEFT_Y, -1);
	action_add_joy_axis("ui_down", JOY_AXIS_LEFT_Y, 1);
	action_add_joy_button("ui_accept", JOY_BUTTON_A);
	action_add_joy_button("ui_cancel", JOY_BUTTON_B);
}

void InputMap::load_from_project_settings() {
	load_default();
	for (auto &kv : ProjectSettings::get_singleton()->get_all_settings()) {
		if (str_begins_with(kv.first, "input_deadzones/")) {
			action_set_deadzone(kv.first.substr(16), (float)kv.second.as_float());
			continue;
		}
		if (!str_begins_with(kv.first, "input/")) {
			continue;
		}
		String action = kv.first.substr(6);
		add_action(action);
		action_erase_codes(action);
		Array keys = kv.second.as_array();
		for (const Variant &k : keys.vec()) {
			int code = k.is_num() ? (int)k.as_int() : input_code_from_name(k.as_string());
			if (code == KEY_NONE) {
				print_warning("Action '" + action + "' : touche inconnue '" + k.as_string() + "'.");
			}
			action_add_code(action, code);
		}
	}
}

void InputMap::_bind_methods() {
	ClassDB::bind_method(D_METHOD("has_action", "action"), &InputMap::has_action);
	ClassDB::bind_method(D_METHOD("add_action", "action"), &InputMap::add_action);
	ClassDB::bind_method(D_METHOD("erase_action", "action"), &InputMap::erase_action);
	ClassDB::bind_method(D_METHOD("action_add_code", "action", "code"), &InputMap::action_add_code);
	ClassDB::bind_method(D_METHOD("action_add_joy_button", "action", "button"), &InputMap::action_add_joy_button);
	ClassDB::bind_method(D_METHOD("action_add_joy_axis", "action", "axis", "sign"), &InputMap::action_add_joy_axis);
	ClassDB::bind_method(D_METHOD("action_set_deadzone", "action", "deadzone"), &InputMap::action_set_deadzone);
	ClassDB::bind_method(D_METHOD("action_get_deadzone", "action"), &InputMap::action_get_deadzone);
	ClassDB::bind_method(D_METHOD("get_actions"), &InputMap::get_actions);
}

InputMap::InputMap() {
	singleton = this;
	load_default();
}

InputMap::~InputMap() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
Input *Input::singleton = nullptr;

void Input::_set_action(const String &p_action, bool p_pressed) {
	ActionState &state = action_states[p_action];
	if (state.pressed == p_pressed) {
		return;
	}
	state.pressed = p_pressed;
	state.strength = state.raw_strength = p_pressed ? 1.0f : 0.0f;
	Engine *engine = Engine::get_singleton();
	if (p_pressed) {
		state.pressed_process_frame = engine->get_process_frames();
		state.pressed_physics_frame = engine->get_physics_frames();
	} else {
		state.released_process_frame = engine->get_process_frames();
		state.released_physics_frame = engine->get_physics_frames();
	}
}

void Input::_update_actions() {
	for (auto &kv : InputMap::get_singleton()->get_action_map()) {
		bool pressed = false;
		float raw = 0.0f; // plus forte valeur d'axe, toutes manettes confondues
		for (int code : kv.second) {
			if (input_code_is_joy_axis(code)) {
				const int axis = (code & ~INPUT_CODE_JOY_AXIS_FLAG) >> 1;
				const bool positive = (code & 1) != 0;
				for (const auto &joy : joypads) {
					const float value = axis < JOY_AXIS_MAX ? joy.second.axes[axis] : 0.0f;
					raw = std::max(raw, positive ? value : -value);
				}
			} else if (codes_pressed.count(code)) {
				pressed = true;
			}
		}
		const float deadzone = InputMap::get_singleton()->action_get_deadzone(kv.first);
		const bool analog = !pressed && raw > 0.0f && raw >= deadzone; // Godot : appuyé dès la zone morte atteinte
		_set_action(kv.first, pressed || analog);
		ActionState &state = action_states[kv.first];
		if (pressed) {
			state.strength = state.raw_strength = 1.0f;
		} else {
			// Comme Godot : la course utile (zone morte -> 1) est ramenée sur 0 -> 1.
			state.raw_strength = raw;
			state.strength = analog ? std::min((raw - deadzone) / std::max(1.0f - deadzone, 0.0001f), 1.0f) : 0.0f;
		}
	}
}

void Input::parse_input_event(const Ref<InputEvent> &p_event) {
	if (p_event.is_null()) {
		return;
	}
	if (InputEventMouse *mouse = Object::cast_to<InputEventMouse>(p_event.ptr())) {
		mouse_position = mouse->get_position();
	}
	if (p_event->is_echo()) {
		return;
	}
	if (InputEventJoypadMotion *motion = Object::cast_to<InputEventJoypadMotion>(p_event.ptr())) {
		if (motion->get_axis() >= 0 && motion->get_axis() < JOY_AXIS_MAX) {
			joypads[motion->get_device()].axes[motion->get_axis()] = std::min(std::max(motion->get_axis_value(), -1.0f), 1.0f);
			_update_actions();
		}
		return;
	}
	if (InputEventJoypadButton *button = Object::cast_to<InputEventJoypadButton>(p_event.ptr())) {
		// Chaque manette garde son état ; pour les actions, un bouton compte s'il est tenu sur l'une d'elles.
		Joypad &joy = joypads[button->get_device()];
		if (button->is_pressed()) {
			joy.buttons.insert(button->get_button_index());
		} else {
			joy.buttons.erase(button->get_button_index());
		}
		bool held = false;
		for (const auto &kv : joypads) {
			held = held || kv.second.buttons.count(button->get_button_index()) > 0;
		}
		if (held) {
			codes_pressed.insert(button->get_input_code());
		} else {
			codes_pressed.erase(button->get_input_code());
		}
		_update_actions();
		return;
	}
	int code = p_event->get_input_code();
	if (code == 0) {
		return;
	}
	if (code == (INPUT_CODE_MOUSE_FLAG | MOUSE_BUTTON_WHEEL_UP) || code == (INPUT_CODE_MOUSE_FLAG | MOUSE_BUTTON_WHEEL_DOWN)) {
		return; // la molette n'a pas d'état "maintenu"
	}
	if (p_event->is_pressed()) {
		codes_pressed.insert(code);
	} else {
		codes_pressed.erase(code);
	}
	_update_actions();
}

void Input::release_all() {
	codes_pressed.clear();
	for (auto &kv : joypads) {
		kv.second.buttons.clear();
		for (float &axis : kv.second.axes) {
			axis = 0.0f;
		}
	}
	for (auto &kv : action_states) {
		kv.second = ActionState();
	}
}

bool Input::is_key_pressed(int p_keycode) const {
	// Les lettres sont stockées en minuscules.
	if (p_keycode >= 'A' && p_keycode <= 'Z') {
		p_keycode += 'a' - 'A';
	}
	return codes_pressed.count(p_keycode) > 0;
}

bool Input::is_mouse_button_pressed(int p_button) const {
	return codes_pressed.count(INPUT_CODE_MOUSE_FLAG | p_button) > 0;
}

static void _warn_unknown_action(const String &p_action) {
	static std::set<String> warned;
	if (!InputMap::get_singleton()->has_action(p_action) && !warned.count(p_action)) {
		warned.insert(p_action);
		print_error("L'action d'entrée '" + p_action + "' n'existe pas (Projet > Paramètres du projet > Actions).");
	}
}

bool Input::is_action_pressed(const String &p_action) const {
	auto it = action_states.find(p_action);
	if (it == action_states.end()) {
		_warn_unknown_action(p_action);
		return false;
	}
	return it->second.pressed;
}

bool Input::is_action_just_pressed(const String &p_action) const {
	auto it = action_states.find(p_action);
	if (it == action_states.end()) {
		_warn_unknown_action(p_action);
		return false;
	}
	Engine *engine = Engine::get_singleton();
	if (!it->second.pressed) {
		return false;
	}
	if (engine->is_in_physics_frame()) {
		return it->second.pressed_physics_frame == engine->get_physics_frames();
	}
	return it->second.pressed_process_frame == engine->get_process_frames();
}

bool Input::is_action_just_released(const String &p_action) const {
	auto it = action_states.find(p_action);
	if (it == action_states.end()) {
		_warn_unknown_action(p_action);
		return false;
	}
	Engine *engine = Engine::get_singleton();
	if (it->second.pressed) {
		return false;
	}
	if (engine->is_in_physics_frame()) {
		return it->second.released_physics_frame == engine->get_physics_frames();
	}
	return it->second.released_process_frame == engine->get_process_frames();
}

float Input::get_action_strength(const String &p_action) const {
	auto it = action_states.find(p_action);
	if (it == action_states.end()) {
		_warn_unknown_action(p_action);
		return 0.0f;
	}
	return it->second.strength;
}

float Input::get_action_raw_strength(const String &p_action) const {
	auto it = action_states.find(p_action);
	if (it == action_states.end()) {
		_warn_unknown_action(p_action);
		return 0.0f;
	}
	return it->second.raw_strength;
}

float Input::get_axis(const String &p_negative, const String &p_positive) const {
	return get_action_strength(p_positive) - get_action_strength(p_negative);
}

// Repris de Input::get_vector de Godot 4.3 : zone morte circulaire, longueur limitée à 1.
Vector2 Input::get_vector(const String &p_neg_x, const String &p_pos_x, const String &p_neg_y, const String &p_pos_y, float p_deadzone) const {
	Vector2 vector = Vector2(
			get_action_raw_strength(p_pos_x) - get_action_raw_strength(p_neg_x),
			get_action_raw_strength(p_pos_y) - get_action_raw_strength(p_neg_y));
	if (p_deadzone < 0.0f) {
		// If the deadzone isn't specified, get it from the average of the actions.
		const InputMap *map = InputMap::get_singleton();
		p_deadzone = 0.25f * (map->action_get_deadzone(p_pos_x) + map->action_get_deadzone(p_neg_x) + map->action_get_deadzone(p_pos_y) + map->action_get_deadzone(p_neg_y));
	}
	// Circular length limiting and deadzone.
	const real_t length = vector.length();
	if (length <= p_deadzone) {
		return Vector2();
	} else if (length > 1.0f) {
		return vector / length;
	} else {
		// Inverse lerp length to map (p_deadzone, 1) to (0, 1).
		return vector * (((length - p_deadzone) / (1.0f - p_deadzone)) / length);
	}
}

// --- Manettes ---
void Input::joy_connection_changed(int p_device, bool p_connected, const String &p_name, const String &p_guid) {
	if (p_connected) {
		Joypad &joy = joypads[p_device];
		joy = Joypad();
		joy.name = p_name;
		joy.guid = p_guid;
	} else {
		if (!joypads.count(p_device)) {
			return;
		}
		// Les boutons tenus sur cette manette sont relâchés.
		const std::set<int> held = joypads[p_device].buttons;
		joypads.erase(p_device);
		for (int button : held) {
			bool still = false;
			for (const auto &kv : joypads) {
				still = still || kv.second.buttons.count(button) > 0;
			}
			if (!still) {
				codes_pressed.erase(input_code_joy_button(button));
			}
		}
		_update_actions();
	}
	emit_signal("joy_connection_changed", p_device, p_connected);
}

Array Input::get_connected_joypads() const {
	Array list;
	for (const auto &kv : joypads) {
		list.push_back(Variant((int64_t)kv.first));
	}
	return list;
}

String Input::get_joy_name(int p_device) const {
	const auto it = joypads.find(p_device);
	return it == joypads.end() ? String() : it->second.name;
}

String Input::get_joy_guid(int p_device) const {
	const auto it = joypads.find(p_device);
	return it == joypads.end() ? String() : it->second.guid;
}

bool Input::is_joy_button_pressed(int p_device, int p_button) const {
	const auto it = joypads.find(p_device);
	return it != joypads.end() && it->second.buttons.count(p_button) > 0;
}

float Input::get_joy_axis(int p_device, int p_axis) const {
	const auto it = joypads.find(p_device);
	return it != joypads.end() && p_axis >= 0 && p_axis < JOY_AXIS_MAX ? it->second.axes[p_axis] : 0.0f;
}

void Input::start_joy_vibration(int p_device, float p_weak, float p_strong, float p_duration) {
	if (joy_vibration_handler && joypads.count(p_device)) {
		joy_vibration_handler(p_device, std::min(std::max(p_weak, 0.0f), 1.0f), std::min(std::max(p_strong, 0.0f), 1.0f), std::max(p_duration, 0.0f));
	}
}

void Input::stop_joy_vibration(int p_device) {
	if (joy_vibration_handler && joypads.count(p_device)) {
		joy_vibration_handler(p_device, 0.0f, 0.0f, 0.0f);
	}
}

void Input::virtual_joy_button(int p_button, bool p_pressed) {
	if (virtual_joy_handler) {
		virtual_joy_handler(0, p_button, p_pressed ? 1.0f : 0.0f);
	}
}

void Input::virtual_joy_axis(int p_axis, float p_value) {
	if (virtual_joy_handler) {
		virtual_joy_handler(1, p_axis, p_value);
	}
}

void Input::virtual_joy_disconnect() {
	if (virtual_joy_handler) {
		virtual_joy_handler(2, 0, 0.0f);
	}
}

void Input::action_press(const String &p_action) {
	_set_action(p_action, true);
}

void Input::action_release(const String &p_action) {
	_set_action(p_action, false);
}

void Input::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_key_pressed", "keycode"), &Input::is_key_pressed);
	ClassDB::bind_method(D_METHOD("is_mouse_button_pressed", "button"), &Input::is_mouse_button_pressed);
	ClassDB::bind_method(D_METHOD("is_action_pressed", "action"), &Input::is_action_pressed);
	ClassDB::bind_method(D_METHOD("is_action_just_pressed", "action"), &Input::is_action_just_pressed);
	ClassDB::bind_method(D_METHOD("is_action_just_released", "action"), &Input::is_action_just_released);
	ClassDB::bind_method(D_METHOD("get_action_strength", "action"), &Input::get_action_strength);
	ClassDB::bind_method(D_METHOD("get_axis", "negative", "positive"), &Input::get_axis);
	ClassDB::bind_method(D_METHOD("get_vector", "neg_x", "pos_x", "neg_y", "pos_y", "deadzone"), &Input::get_vector, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("get_action_raw_strength", "action"), &Input::get_action_raw_strength);
	ClassDB::bind_method(D_METHOD("parse_input_event", "event"), &Input::parse_input_event);
	ClassDB::bind_method(D_METHOD("get_connected_joypads"), &Input::get_connected_joypads);
	ClassDB::bind_method(D_METHOD("get_joy_name", "device"), &Input::get_joy_name);
	ClassDB::bind_method(D_METHOD("get_joy_guid", "device"), &Input::get_joy_guid);
	ClassDB::bind_method(D_METHOD("is_joy_button_pressed", "device", "button"), &Input::is_joy_button_pressed);
	ClassDB::bind_method(D_METHOD("get_joy_axis", "device", "axis"), &Input::get_joy_axis);
	ClassDB::bind_method(D_METHOD("start_joy_vibration", "device", "weak_magnitude", "strong_magnitude", "duration"), &Input::start_joy_vibration, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop_joy_vibration", "device"), &Input::stop_joy_vibration);
	ClassDB::bind_method(D_METHOD("virtual_joy_button", "button", "pressed"), &Input::virtual_joy_button);
	ClassDB::bind_method(D_METHOD("virtual_joy_axis", "axis", "value"), &Input::virtual_joy_axis);
	ClassDB::bind_method(D_METHOD("virtual_joy_disconnect"), &Input::virtual_joy_disconnect);
	ADD_SIGNAL(MethodInfo("joy_connection_changed", PropertyInfo(Variant::INT, "device"), PropertyInfo(Variant::BOOL, "connected")));
	ClassDB::bind_method(D_METHOD("get_mouse_position"), &Input::get_mouse_position);
	ClassDB::bind_method(D_METHOD("set_mouse_mode", "mode"), &Input::set_mouse_mode);
	ClassDB::bind_method(D_METHOD("get_mouse_mode"), &Input::get_mouse_mode);
	BIND_CONSTANT(MOUSE_MODE_VISIBLE);
	BIND_CONSTANT(MOUSE_MODE_HIDDEN);
	BIND_CONSTANT(MOUSE_MODE_CAPTURED);
	ClassDB::bind_method(D_METHOD("action_press", "action"), &Input::action_press);
	ClassDB::bind_method(D_METHOD("action_release", "action"), &Input::action_release);
}

void Input::set_mouse_mode(int p_mode) {
	mouse_mode = p_mode < 0 ? 0 : (p_mode > 2 ? 2 : p_mode);
	if (mouse_mode_handler) {
		mouse_mode_handler(mouse_mode);
	}
}

Input::Input() {
	singleton = this;
}

Input::~Input() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
