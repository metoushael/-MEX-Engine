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

String input_code_get_name(int p_code) {
	for (auto &kv : _named_codes()) {
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
	int code = p_event->get_input_code();
	return code != 0 && std::find(it->second.begin(), it->second.end(), code) != it->second.end();
}

void InputMap::load_default() {
	actions.clear();
	action_add_code("ui_left", KEY_LEFT);
	action_add_code("ui_right", KEY_RIGHT);
	action_add_code("ui_up", KEY_UP);
	action_add_code("ui_down", KEY_DOWN);
	action_add_code("ui_accept", KEY_ENTER);
	action_add_code("ui_accept", KEY_SPACE);
	action_add_code("ui_cancel", KEY_ESCAPE);
}

void InputMap::load_from_project_settings() {
	load_default();
	for (auto &kv : ProjectSettings::get_singleton()->get_all_settings()) {
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
		for (int code : kv.second) {
			if (codes_pressed.count(code)) {
				pressed = true;
				break;
			}
		}
		_set_action(kv.first, pressed);
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
	return is_action_pressed(p_action) ? 1.0f : 0.0f;
}

float Input::get_axis(const String &p_negative, const String &p_positive) const {
	return get_action_strength(p_positive) - get_action_strength(p_negative);
}

Vector2 Input::get_vector(const String &p_neg_x, const String &p_pos_x, const String &p_neg_y, const String &p_pos_y) const {
	Vector2 v(get_axis(p_neg_x, p_pos_x), get_axis(p_neg_y, p_pos_y));
	return v.length_squared() > 1.0f ? v.normalized() : v;
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
	ClassDB::bind_method(D_METHOD("get_vector", "neg_x", "pos_x", "neg_y", "pos_y"), &Input::get_vector);
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
