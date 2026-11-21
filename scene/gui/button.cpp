#include "button.h"

#include "scene/main/scene_tree.h"

Button::Button() {
	set_size(Vector2(120, 36));
}

Size2 Button::get_minimum_size() const {
	Ref<Font> f = font.is_valid() ? font : Font::get_default();
	return f->get_string_size(text, font_size) + Size2(16, 8);
}

void Button::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_input(!get_tree()->is_edit_mode());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			hovered = false;
			pressing = false;
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (!is_visible()) {
				hovered = false;
				pressing = false;
			}
		} break;
		case NOTIFICATION_DRAW: {
			const Size2 size = get_size();
			Color bg = color_normal;
			if (disabled) {
				bg = Color(color_normal.r * 0.7f, color_normal.g * 0.7f, color_normal.b * 0.7f, color_normal.a * 0.7f);
			} else if (pressing || toggled_on) {
				bg = color_pressed;
			} else if (hovered) {
				bg = color_hover;
			}
			draw_rect(Rect2(Vector2(), size), bg, true);
			draw_rect(Rect2(Vector2(), size), Color(1, 1, 1, hovered && !disabled ? 0.35f : 0.12f), false, 1.0f);

			Ref<Font> f = font.is_valid() ? font : Font::get_default();
			Size2 text_size = f->get_string_size(text, font_size);
			Vector2 pos = ((size - text_size) * 0.5f).round();
			Color fc = font_color;
			if (disabled) {
				fc.a *= 0.5f;
			}
			draw_string(f, pos, text, font_size, fc);
		} break;
	}
}

void Button::input(const Ref<InputEvent> &p_event) {
	if (disabled || !is_visible_in_tree()) {
		return;
	}
	if (InputEventMouseMotion *motion = Object::cast_to<InputEventMouseMotion>(p_event.ptr())) {
		bool inside = has_point(_event_to_local(motion->get_position()));
		if (inside != hovered) {
			hovered = inside;
			notification(hovered ? NOTIFICATION_MOUSE_ENTER : NOTIFICATION_MOUSE_EXIT);
			emit_signal(hovered ? "mouse_entered" : "mouse_exited");
			queue_redraw();
		}
		return;
	}
	InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(p_event.ptr());
	if (!mb || mb->get_button_index() != MOUSE_BUTTON_LEFT) {
		return;
	}
	const bool inside = has_point(_event_to_local(mb->get_position()));
	if (mb->is_pressed()) {
		if (inside) {
			pressing = true;
			queue_redraw();
			get_tree()->set_input_as_handled();
			emit_signal("button_down");
		}
	} else if (pressing) {
		pressing = false;
		queue_redraw();
		const ObjectID self_id = get_instance_id();
		emit_signal("button_up");
		if (!ObjectDB::get_instance(self_id)) {
			return;
		}
		if (inside) {
			get_tree()->set_input_as_handled();
			if (toggle_mode) {
				toggled_on = !toggled_on;
				emit_signal("toggled", toggled_on);
				if (!ObjectDB::get_instance(self_id)) {
					return;
				}
			}
			emit_signal("pressed");
		}
	}
}

void Button::set_text(const String &p_text) {
	text = p_text;
	queue_redraw();
	update_minimum_size();
}

void Button::set_font(const Ref<Font> &p_font) {
	font = p_font;
	queue_redraw();
	update_minimum_size();
}

void Button::set_font_size(int p_size) {
	font_size = Math::clamp(p_size, 4, 256);
	queue_redraw();
	update_minimum_size();
}

void Button::set_disabled(bool p_disabled) {
	disabled = p_disabled;
	if (disabled) {
		hovered = false;
		pressing = false;
	}
	queue_redraw();
}

void Button::set_pressed(bool p_pressed) {
	if (toggle_mode && toggled_on != p_pressed) {
		toggled_on = p_pressed;
		queue_redraw();
	}
}

void Button::set_color_normal(const Color &p_color) {
	color_normal = p_color;
	queue_redraw();
}

void Button::set_color_hover(const Color &p_color) {
	color_hover = p_color;
	queue_redraw();
}

void Button::set_color_pressed(const Color &p_color) {
	color_pressed = p_color;
	queue_redraw();
}

void Button::set_font_color(const Color &p_color) {
	font_color = p_color;
	queue_redraw();
}

void Button::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_text", "text"), &Button::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &Button::get_text);
	ClassDB::bind_method(D_METHOD("set_font", "font"), &Button::set_font);
	ClassDB::bind_method(D_METHOD("get_font"), &Button::get_font);
	ClassDB::bind_method(D_METHOD("set_font_size", "size"), &Button::set_font_size);
	ClassDB::bind_method(D_METHOD("get_font_size"), &Button::get_font_size);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &Button::set_disabled);
	ClassDB::bind_method(D_METHOD("is_disabled"), &Button::is_disabled);
	ClassDB::bind_method(D_METHOD("set_toggle_mode", "enabled"), &Button::set_toggle_mode);
	ClassDB::bind_method(D_METHOD("is_toggle_mode"), &Button::is_toggle_mode);
	ClassDB::bind_method(D_METHOD("set_pressed", "pressed"), &Button::set_pressed);
	ClassDB::bind_method(D_METHOD("is_pressed"), &Button::is_pressed);
	ClassDB::bind_method(D_METHOD("is_hovered"), &Button::is_hovered);
	ClassDB::bind_method(D_METHOD("set_color_normal", "color"), &Button::set_color_normal);
	ClassDB::bind_method(D_METHOD("get_color_normal"), &Button::get_color_normal);
	ClassDB::bind_method(D_METHOD("set_color_hover", "color"), &Button::set_color_hover);
	ClassDB::bind_method(D_METHOD("get_color_hover"), &Button::get_color_hover);
	ClassDB::bind_method(D_METHOD("set_color_pressed", "color"), &Button::set_color_pressed);
	ClassDB::bind_method(D_METHOD("get_color_pressed"), &Button::get_color_pressed);
	ClassDB::bind_method(D_METHOD("set_font_color", "color"), &Button::set_font_color);
	ClassDB::bind_method(D_METHOD("get_font_color"), &Button::get_font_color);

	ADD_GROUP("Button");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "text"), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "font", PROPERTY_HINT_RESOURCE_TYPE, "Font"), "set_font", "get_font");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "font_size", PROPERTY_HINT_RANGE, "4,256,1"), "set_font_size", "get_font_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "disabled"), "set_disabled", "is_disabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "toggle_mode"), "set_toggle_mode", "is_toggle_mode");
	ADD_GROUP("Colors");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color_normal"), "set_color_normal", "get_color_normal");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color_hover"), "set_color_hover", "get_color_hover");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color_pressed"), "set_color_pressed", "get_color_pressed");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "font_color"), "set_font_color", "get_font_color");

	ADD_SIGNAL(MethodInfo("pressed"));
	ADD_SIGNAL(MethodInfo("button_down"));
	ADD_SIGNAL(MethodInfo("button_up"));
	ADD_SIGNAL(MethodInfo("toggled", PropertyInfo(Variant::BOOL, "toggled_on")));
	ADD_SIGNAL(MethodInfo("mouse_entered"));
	ADD_SIGNAL(MethodInfo("mouse_exited"));
}
