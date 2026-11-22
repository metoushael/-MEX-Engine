#include "line_edit.h"

#include "core/input/keyboard.h"
#include "scene/main/scene_tree.h"


LineEdit::LineEdit() {
	set_size(Vector2(200, 32));
	set_focus_mode(FOCUS_ALL);
}

Size2 LineEdit::get_minimum_size() const {
	return Size2(40, Font::get_default()->get_height(font_size) + 10);
}

int LineEdit::_prev_char(int p_pos) const {
	int pos = p_pos - 1;
	while (pos > 0 && ((unsigned char)text[pos] & 0xC0) == 0x80) {
		pos--;
	}
	return std::max(0, pos);
}

int LineEdit::_next_char(int p_pos) const {
	int pos = p_pos + 1;
	while (pos < (int)text.size() && ((unsigned char)text[pos] & 0xC0) == 0x80) {
		pos++;
	}
	return std::min(pos, (int)text.size());
}

int LineEdit::_char_count() const {
	int count = 0;
	for (unsigned char c : text) {
		if ((c & 0xC0) != 0x80) {
			count++;
		}
	}
	return count;
}

void LineEdit::_set_text_internal(const String &p_text) {
	text = p_text;
	caret = std::min(caret, (int)text.size());
	blink_time = 0;
	queue_redraw();
	emit_signal("text_changed", text);
}

void LineEdit::set_text(const String &p_text) {
	if (text == p_text) {
		return;
	}
	text = p_text;
	caret = (int)text.size();
	queue_redraw();
}

void LineEdit::set_placeholder(const String &p_text) {
	placeholder_text = p_text;
	queue_redraw();
}

void LineEdit::set_editable(bool p_editable) {
	editable = p_editable;
	if (!editable) {
		release_focus();
	}
	queue_redraw();
}

void LineEdit::set_font_size(int p_size) {
	font_size = std::max(4, p_size);
	update_minimum_size();
	queue_redraw();
}

void LineEdit::set_font_color(const Color &p_color) {
	font_color = p_color;
	queue_redraw();
}

void LineEdit::set_bg_color(const Color &p_color) {
	bg_color = p_color;
	queue_redraw();
}

void LineEdit::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			const bool in_game = !get_tree()->is_edit_mode();
			set_process_input(in_game);
			set_process_internal(in_game);
		} break;
		case NOTIFICATION_FOCUS_ENTER: {
			blink_time = 0;
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (has_focus()) {
				const bool before = std::fmod(blink_time, 1.0) < 0.5;
				blink_time += get_process_delta_time();
				if ((std::fmod(blink_time, 1.0) < 0.5) != before) {
					queue_redraw();
				}
			}
		} break;
		case NOTIFICATION_DRAW: {
			const int font_size = get_theme_font_size("font_size", this->font_size);
			const Color font_color = get_theme_color("font_color", this->font_color);
			const Color bg_color = get_theme_color("bg_color", this->bg_color);
			const Size2 size = get_size();
			Ref<Font> f = Font::get_default();
			draw_rect(Rect2(Vector2(), size), bg_color, true);
			draw_rect(Rect2(Vector2(), size), has_focus() ? get_theme_color("focus_color", Color(0.45f, 0.65f, 1.0f, 0.95f)) : Color(1, 1, 1, 0.25f), false, 1.0f);
			const real_t text_height = f->get_height(font_size);
			const real_t y = std::round((size.y - text_height) * 0.5f);
			const real_t padding = 6;
			// Le texte défile pour que le curseur reste visible.
			const real_t caret_x = f->get_string_size(text.substr(0, caret), font_size).x;
			const real_t scroll = std::max((real_t)0, caret_x - (size.x - padding * 2));
			if (text.empty() && !placeholder_text.empty()) {
				draw_string(f, Vector2(padding, y), placeholder_text, font_size, Color(font_color.r, font_color.g, font_color.b, font_color.a * 0.4f));
			} else {
				// Découpe grossière : on ne dessine que les caractères entièrement visibles.
				String visible;
				real_t x = -scroll;
				real_t start_x = 0;
				bool started = false;
				int pos = 0;
				while (pos < (int)text.size()) {
					const int next = _next_char(pos);
					const String ch = text.substr(pos, next - pos);
					const real_t w = f->get_string_size(ch, font_size).x;
					if (x >= 0 && x + w <= size.x - padding * 2 + 0.5f) {
						if (!started) {
							started = true;
							start_x = x;
						}
						visible += ch;
					}
					x += w;
					pos = next;
				}
				draw_string(f, Vector2(padding + start_x, y), visible, font_size, font_color);
			}
			if (has_focus() && std::fmod(blink_time, 1.0) < 0.5) {
				const real_t cx = padding + caret_x - scroll;
				draw_line(Vector2(cx, y), Vector2(cx, y + text_height), font_color, 1.0f);
			}
		} break;
	}
}

void LineEdit::input(const Ref<InputEvent> &p_event) {
	if (!is_visible_in_tree()) {
		return;
	}
	if (InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(p_event.ptr())) {
		if (mb->get_button_index() != MOUSE_BUTTON_LEFT || !mb->is_pressed()) {
			return;
		}
		const Vector2 local = _event_to_local(mb->get_position());
		if (_hit(mb->get_position()) && editable) {
			grab_focus();
			// Place le curseur sous la souris.
			Ref<Font> f = Font::get_default();
			const real_t caret_x = f->get_string_size(text.substr(0, caret), font_size).x;
			const real_t scroll = std::max((real_t)0, caret_x - (get_size().x - 12));
			const real_t target = local.x - 6 + scroll;
			int pos = 0;
			while (pos < (int)text.size()) {
				const int next = _next_char(pos);
				const real_t mid = (f->get_string_size(text.substr(0, pos), font_size).x + f->get_string_size(text.substr(0, next), font_size).x) * 0.5f;
				if (target < mid) {
					break;
				}
				pos = next;
			}
			caret = pos;
			blink_time = 0;
			queue_redraw();
			get_tree()->set_input_as_handled();
		} else {
			release_focus();
		}
		return;
	}
	InputEventKey *key = Object::cast_to<InputEventKey>(p_event.ptr());
	if (!key || !has_focus() || !editable || !key->is_pressed()) {
		return;
	}
	if (key->get_keycode() == KEY_TAB || key->get_keycode() == KEY_UP || key->get_keycode() == KEY_DOWN) {
		return; // laissées à la navigation entre contrôles
	}
	get_tree()->set_input_as_handled();
	const String typed = key->get_text();
	if (!typed.empty()) {
		String filtered;
		for (char c : typed) {
			if ((unsigned char)c >= 32) {
				filtered += c;
			}
		}
		if (filtered.empty()) {
			return;
		}
		String new_text = text.substr(0, caret) + filtered + text.substr(caret);
		const int old_caret = caret;
		caret += (int)filtered.size();
		if (max_length > 0) {
			// Retire les caractères en trop (par la fin de ce qui vient d'être saisi).
			String probe = new_text;
			std::swap(text, probe);
			while (_char_count() > max_length && caret > old_caret) {
				const int prev = _prev_char(caret);
				text.erase(prev, caret - prev);
				caret = prev;
			}
			new_text = text;
			std::swap(text, probe);
		}
		if (new_text != text) {
			_set_text_internal(new_text);
		}
		return;
	}
	switch (key->get_keycode()) {
		case KEY_BACKSPACE: {
			if (caret > 0) {
				const int prev = _prev_char(caret);
				String new_text = text;
				new_text.erase(prev, caret - prev);
				caret = prev;
				_set_text_internal(new_text);
			}
		} break;
		case KEY_DELETE: {
			if (caret < (int)text.size()) {
				const int next = _next_char(caret);
				String new_text = text;
				new_text.erase(caret, next - caret);
				_set_text_internal(new_text);
			}
		} break;
		case KEY_LEFT: {
			caret = _prev_char(caret);
			blink_time = 0;
			queue_redraw();
		} break;
		case KEY_RIGHT: {
			caret = _next_char(caret);
			blink_time = 0;
			queue_redraw();
		} break;
		case KEY_ENTER: {
			if (!key->is_echo()) {
				emit_signal("text_submitted", text);
			}
		} break;
		case KEY_ESCAPE: {
			release_focus();
		} break;
		default:
			break;
	}
}

void LineEdit::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_text", "text"), &LineEdit::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &LineEdit::get_text);
	ClassDB::bind_method(D_METHOD("set_placeholder", "text"), &LineEdit::set_placeholder);
	ClassDB::bind_method(D_METHOD("get_placeholder"), &LineEdit::get_placeholder);
	ClassDB::bind_method(D_METHOD("set_max_length", "length"), &LineEdit::set_max_length);
	ClassDB::bind_method(D_METHOD("get_max_length"), &LineEdit::get_max_length);
	ClassDB::bind_method(D_METHOD("set_editable", "editable"), &LineEdit::set_editable);
	ClassDB::bind_method(D_METHOD("is_editable"), &LineEdit::is_editable);
	ClassDB::bind_method(D_METHOD("set_font_size", "size"), &LineEdit::set_font_size);
	ClassDB::bind_method(D_METHOD("get_font_size"), &LineEdit::get_font_size);
	ClassDB::bind_method(D_METHOD("set_font_color", "color"), &LineEdit::set_font_color);
	ClassDB::bind_method(D_METHOD("get_font_color"), &LineEdit::get_font_color);
	ClassDB::bind_method(D_METHOD("set_bg_color", "color"), &LineEdit::set_bg_color);
	ClassDB::bind_method(D_METHOD("get_bg_color"), &LineEdit::get_bg_color);
	ClassDB::bind_method(D_METHOD("clear"), &LineEdit::clear);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "text"), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "placeholder_text"), "set_placeholder", "get_placeholder");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_length", PROPERTY_HINT_RANGE, "0,1000,1"), "set_max_length", "get_max_length");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "editable"), "set_editable", "is_editable");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "font_size", PROPERTY_HINT_RANGE, "4,128,1"), "set_font_size", "get_font_size");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "font_color"), "set_font_color", "get_font_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bg_color"), "set_bg_color", "get_bg_color");

	ADD_SIGNAL(MethodInfo("text_changed", PropertyInfo(Variant::STRING, "new_text")));
	ADD_SIGNAL(MethodInfo("text_submitted", PropertyInfo(Variant::STRING, "text")));
}
