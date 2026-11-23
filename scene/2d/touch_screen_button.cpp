#include "touch_screen_button.h"

#include "core/config/engine.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "scene/main/scene_tree.h"

#include <algorithm>
#include <cmath>

// Écran -> repère du nœud (caméra ou CanvasLayer, puis transformation du nœud).
static Vector2 _screen_to_local(const CanvasItem *p_item, const Vector2 &p_screen) {
	return p_item->get_global_transform().affine_inverse().xform(p_item->get_canvas_transform().affine_inverse().xform(p_screen));
}

static bool _touch_controls_shown(int p_mode) {
	return p_mode == TouchScreenButton::VISIBILITY_ALWAYS || Engine::get_singleton()->is_editor_hint() || Input::get_singleton()->is_touchscreen_available();
}

// ---------------------------------------------------------------------------
// TouchScreenButton
// ---------------------------------------------------------------------------
Rect2 TouchScreenButton::get_rect() const {
	if (texture_normal.is_valid()) {
		return Rect2(Vector2(), texture_normal->get_size());
	}
	return Rect2(Vector2(), Vector2(radius * 2, radius * 2));
}

void TouchScreenButton::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_input(!get_tree()->is_edit_mode());
			queue_redraw();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (is_pressed()) {
				_release(true);
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (!is_visible_in_tree() && is_pressed()) {
				_release();
			}
		} break;
		case NOTIFICATION_DRAW: {
			if (!_touch_controls_shown(visibility)) {
				return;
			}
			if (finger_pressed != -1 && texture_pressed.is_valid()) {
				draw_texture(texture_pressed, Vector2());
			} else if (texture_normal.is_valid()) {
				draw_texture(texture_normal, Vector2(), finger_pressed != -1 ? Color(0.7f, 0.7f, 0.7f, 1) : Color(1, 1, 1, 1));
			} else {
				// Sans texture : un disque translucide, plus clair quand il est enfoncé.
				const Vector2 center(radius, radius);
				draw_circle(center, radius, Color(1, 1, 1, finger_pressed != -1 ? 0.55f : 0.25f));
				draw_arc(center, radius, 0, 6.2831853f, 48, Color(1, 1, 1, 0.8f), 2);
			}
		} break;
	}
}

bool TouchScreenButton::_is_point_inside(const Vector2 &p_point) const {
	const Vector2 coord = _screen_to_local(this, p_point);
	if (texture_normal.is_valid()) {
		return Rect2(Vector2(), texture_normal->get_size()).has_point(coord);
	}
	return (coord - Vector2(radius, radius)).length() <= radius;
}

void TouchScreenButton::input(const Ref<InputEvent> &p_event) {
	if (!is_visible_in_tree() || !_touch_controls_shown(visibility)) {
		return;
	}

	const InputEventScreenTouch *st = Object::cast_to<InputEventScreenTouch>(p_event.ptr());

	if (passby_press) {
		const InputEventScreenDrag *sd = Object::cast_to<InputEventScreenDrag>(p_event.ptr());

		if (st && !st->is_pressed() && finger_pressed == st->get_index()) {
			_release();
		}

		if ((st && st->is_pressed()) || sd) {
			int index = st ? st->get_index() : sd->get_index();
			Vector2 coord = st ? st->get_position() : sd->get_position();

			if (finger_pressed == -1 || index == finger_pressed) {
				if (_is_point_inside(coord)) {
					if (finger_pressed == -1) {
						_press(index);
					}
				} else {
					if (finger_pressed != -1) {
						_release();
					}
				}
			}
		}

	} else {
		if (st) {
			if (st->is_pressed()) {
				const bool can_press = finger_pressed == -1;
				if (!can_press) {
					return; //already fingering
				}

				if (_is_point_inside(st->get_position())) {
					_press(st->get_index());
				}
			} else {
				if (st->get_index() == finger_pressed) {
					_release();
				}
			}
		}
	}
}

void TouchScreenButton::_press(int p_finger) {
	finger_pressed = p_finger;
	if (!action.empty()) {
		Input::get_singleton()->action_press(action);
	}
	emit_signal("pressed");
	queue_redraw();
}

void TouchScreenButton::_release(bool p_exiting_tree) {
	finger_pressed = -1;
	if (!action.empty()) {
		Input::get_singleton()->action_release(action);
	}
	if (!p_exiting_tree) {
		emit_signal("released");
		queue_redraw();
	}
}

void TouchScreenButton::set_texture_normal(const Ref<Texture2D> &p_texture) {
	texture_normal = p_texture;
	queue_redraw();
}

void TouchScreenButton::set_texture_pressed(const Ref<Texture2D> &p_texture) {
	texture_pressed = p_texture;
	queue_redraw();
}

void TouchScreenButton::set_radius(real_t p_radius) {
	radius = std::max(p_radius, (real_t)1);
	queue_redraw();
}

void TouchScreenButton::set_visibility_mode(int p_mode) {
	visibility = p_mode;
	queue_redraw();
}

void TouchScreenButton::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture_normal", "texture"), &TouchScreenButton::set_texture_normal);
	ClassDB::bind_method(D_METHOD("get_texture_normal"), &TouchScreenButton::get_texture_normal);
	ClassDB::bind_method(D_METHOD("set_texture_pressed", "texture"), &TouchScreenButton::set_texture_pressed);
	ClassDB::bind_method(D_METHOD("get_texture_pressed"), &TouchScreenButton::get_texture_pressed);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &TouchScreenButton::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &TouchScreenButton::get_radius);
	ClassDB::bind_method(D_METHOD("set_action", "action"), &TouchScreenButton::set_action);
	ClassDB::bind_method(D_METHOD("get_action"), &TouchScreenButton::get_action);
	ClassDB::bind_method(D_METHOD("set_passby_press", "enabled"), &TouchScreenButton::set_passby_press);
	ClassDB::bind_method(D_METHOD("is_passby_press_enabled"), &TouchScreenButton::is_passby_press_enabled);
	ClassDB::bind_method(D_METHOD("set_visibility_mode", "mode"), &TouchScreenButton::set_visibility_mode);
	ClassDB::bind_method(D_METHOD("get_visibility_mode"), &TouchScreenButton::get_visibility_mode);
	ClassDB::bind_method(D_METHOD("is_pressed"), &TouchScreenButton::is_pressed);
	ClassDB::bind_method(D_METHOD("get_rect"), &TouchScreenButton::get_rect);

	ADD_GROUP("TouchScreenButton");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture_normal", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture_normal", "get_texture_normal");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture_pressed", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture_pressed", "get_texture_pressed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "passby_press"), "set_passby_press", "is_passby_press_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "action"), "set_action", "get_action");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_mode", PROPERTY_HINT_ENUM, "Always,TouchScreen Only"), "set_visibility_mode", "get_visibility_mode");

	ADD_SIGNAL(MethodInfo("pressed"));
	ADD_SIGNAL(MethodInfo("released"));

	BIND_ENUM_CONSTANT(VISIBILITY_ALWAYS);
	BIND_ENUM_CONSTANT(VISIBILITY_TOUCHSCREEN_ONLY);
}

// ---------------------------------------------------------------------------
// VirtualJoystick
// ---------------------------------------------------------------------------
Vector2 VirtualJoystick::_to_local(const Vector2 &p_screen) const {
	return _screen_to_local(this, p_screen);
}

void VirtualJoystick::_set_output(const Vector2 &p_output) {
	output = p_output;
	Input *input = Input::get_singleton();
	const std::pair<const String *, real_t> actions[] = {
		{ &action_left, -output.x }, { &action_right, output.x }, { &action_up, -output.y }, { &action_down, output.y }
	};
	for (const auto &entry : actions) {
		if (entry.first->empty()) {
			continue;
		}
		input->action_set_axis_value(*entry.first, std::max(entry.second, (real_t)0));
	}
	queue_redraw();
}

void VirtualJoystick::input(const Ref<InputEvent> &p_event) {
	if (!is_visible_in_tree() || !_touch_controls_shown(visibility)) {
		return;
	}
	Vector2 position;
	bool moved = false;
	if (const InputEventScreenTouch *st = Object::cast_to<InputEventScreenTouch>(p_event.ptr())) {
		if (st->is_pressed()) {
			// Le doigt doit se poser dans le cercle (avec une marge) pour prendre le stick.
			if (finger != -1 || _to_local(st->get_position()).length() > radius * 1.25f) {
				return;
			}
			finger = st->get_index();
			position = st->get_position();
			moved = true;
		} else if (st->get_index() == finger) {
			finger = -1;
			_set_output(Vector2());
			return;
		}
	} else if (const InputEventScreenDrag *sd = Object::cast_to<InputEventScreenDrag>(p_event.ptr())) {
		if (sd->get_index() == finger) {
			position = sd->get_position();
			moved = true;
		}
	}
	if (!moved) {
		return;
	}
	// Déplacement du doigt rapporté au rayon, limité au cercle.
	Vector2 local = _to_local(position) / radius;
	const real_t length = local.length();
	_set_output(length > 1 ? local / length : local);
}

void VirtualJoystick::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_input(!get_tree()->is_edit_mode());
			queue_redraw();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (finger != -1) {
				finger = -1;
				_set_output(Vector2());
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (!is_visible_in_tree() && finger != -1) {
				finger = -1;
				_set_output(Vector2());
			}
		} break;
		case NOTIFICATION_DRAW: {
			if (!_touch_controls_shown(visibility)) {
				return;
			}
			draw_circle(Vector2(), radius, Color(1, 1, 1, 0.18f));
			draw_arc(Vector2(), radius, 0, 6.2831853f, 64, Color(1, 1, 1, 0.7f), 2);
			draw_circle(output * radius, radius * 0.42f, Color(1, 1, 1, finger != -1 ? 0.75f : 0.45f)); // suit le doigt
		} break;
	}
}

void VirtualJoystick::set_radius(real_t p_radius) {
	radius = std::max(p_radius, (real_t)8);
	queue_redraw();
}

void VirtualJoystick::set_visibility_mode(int p_mode) {
	visibility = p_mode;
	queue_redraw();
}

void VirtualJoystick::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &VirtualJoystick::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &VirtualJoystick::get_radius);
	ClassDB::bind_method(D_METHOD("set_action_left", "action"), &VirtualJoystick::set_action_left);
	ClassDB::bind_method(D_METHOD("get_action_left"), &VirtualJoystick::get_action_left);
	ClassDB::bind_method(D_METHOD("set_action_right", "action"), &VirtualJoystick::set_action_right);
	ClassDB::bind_method(D_METHOD("get_action_right"), &VirtualJoystick::get_action_right);
	ClassDB::bind_method(D_METHOD("set_action_up", "action"), &VirtualJoystick::set_action_up);
	ClassDB::bind_method(D_METHOD("get_action_up"), &VirtualJoystick::get_action_up);
	ClassDB::bind_method(D_METHOD("set_action_down", "action"), &VirtualJoystick::set_action_down);
	ClassDB::bind_method(D_METHOD("get_action_down"), &VirtualJoystick::get_action_down);
	ClassDB::bind_method(D_METHOD("set_visibility_mode", "mode"), &VirtualJoystick::set_visibility_mode);
	ClassDB::bind_method(D_METHOD("get_visibility_mode"), &VirtualJoystick::get_visibility_mode);
	ClassDB::bind_method(D_METHOD("get_output"), &VirtualJoystick::get_output);
	ClassDB::bind_method(D_METHOD("is_pressed"), &VirtualJoystick::is_pressed);

	ADD_GROUP("VirtualJoystick");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "action_left"), "set_action_left", "get_action_left");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "action_right"), "set_action_right", "get_action_right");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "action_up"), "set_action_up", "get_action_up");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "action_down"), "set_action_down", "get_action_down");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_mode", PROPERTY_HINT_ENUM, "Always,TouchScreen Only"), "set_visibility_mode", "get_visibility_mode");
}
