#include "label.h"

#include "core/string/ustring.h"

Label::Label() {
	set_size(Vector2(120, 24));
}

Size2 Label::get_minimum_size() const {
	Ref<Font> f = get_theme_font("font", font);
	return f->get_string_size(atr(text), get_theme_font_size("font_size", font_size));
}

Rect2 Label::_edit_get_rect() const {
	Size2 min = get_minimum_size();
	Size2 s = get_size();
	return Rect2(Vector2(), Size2(std::max(s.x, min.x), std::max(s.y, min.y)));
}

void Label::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW) {
		if (text.empty()) {
			return;
		}
		Ref<Font> f = get_theme_font("font", font);
		const String text = atr(this->text);
		const int font_size = get_theme_font_size("font_size", this->font_size);
		const Color font_color = get_theme_color("font_color", this->font_color);
		const Size2 size = get_size();
		const real_t line_height = f->get_height(font_size);
		std::vector<String> lines = str_split(text, "\n", true);
		const real_t total_height = line_height * (real_t)lines.size();
		real_t y = 0;
		if (vertical_alignment == VALIGN_CENTER) {
			y = (size.y - total_height) * 0.5f;
		} else if (vertical_alignment == VALIGN_BOTTOM) {
			y = size.y - total_height;
		}
		for (const String &line : lines) {
			real_t x = 0;
			if (horizontal_alignment != ALIGN_LEFT) {
				const real_t w = f->get_string_size(line, font_size).x;
				x = horizontal_alignment == ALIGN_CENTER ? (size.x - w) * 0.5f : size.x - w;
			}
			Vector2 pos(std::round(x), std::round(y));
			if (shadow_color.a > 0) {
				draw_string(f, pos + Vector2(1, 1), line, font_size, shadow_color);
			}
			draw_string(f, pos, line, font_size, font_color);
			y += line_height;
		}
	}
}

void Label::set_text(const String &p_text) {
	if (text == p_text) {
		return;
	}
	text = p_text;
	queue_redraw();
	update_minimum_size();
}

void Label::set_font(const Ref<Font> &p_font) {
	font = p_font;
	queue_redraw();
	update_minimum_size();
}

void Label::set_font_size(int p_size) {
	font_size = Math::clamp(p_size, 4, 256);
	queue_redraw();
	update_minimum_size();
}

void Label::set_font_color(const Color &p_color) {
	font_color = p_color;
	queue_redraw();
}

void Label::set_shadow_color(const Color &p_color) {
	shadow_color = p_color;
	queue_redraw();
}

void Label::set_horizontal_alignment(HorizontalAlignment p_align) {
	horizontal_alignment = p_align;
	queue_redraw();
}

void Label::set_vertical_alignment(VerticalAlignment p_align) {
	vertical_alignment = p_align;
	queue_redraw();
}

void Label::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_text", "text"), &Label::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &Label::get_text);
	ClassDB::bind_method(D_METHOD("set_font", "font"), &Label::set_font);
	ClassDB::bind_method(D_METHOD("get_font"), &Label::get_font);
	ClassDB::bind_method(D_METHOD("set_font_size", "size"), &Label::set_font_size);
	ClassDB::bind_method(D_METHOD("get_font_size"), &Label::get_font_size);
	ClassDB::bind_method(D_METHOD("set_font_color", "color"), &Label::set_font_color);
	ClassDB::bind_method(D_METHOD("get_font_color"), &Label::get_font_color);
	ClassDB::bind_method(D_METHOD("set_shadow_color", "color"), &Label::set_shadow_color);
	ClassDB::bind_method(D_METHOD("get_shadow_color"), &Label::get_shadow_color);
	ClassDB::bind_method(D_METHOD("set_horizontal_alignment", "alignment"), &Label::set_horizontal_alignment);
	ClassDB::bind_method(D_METHOD("get_horizontal_alignment"), &Label::get_horizontal_alignment);
	ClassDB::bind_method(D_METHOD("set_vertical_alignment", "alignment"), &Label::set_vertical_alignment);
	ClassDB::bind_method(D_METHOD("get_vertical_alignment"), &Label::get_vertical_alignment);

	ADD_GROUP("Text");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "text", PROPERTY_HINT_MULTILINE_TEXT), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "font", PROPERTY_HINT_RESOURCE_TYPE, "Font"), "set_font", "get_font");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "font_size", PROPERTY_HINT_RANGE, "4,256,1"), "set_font_size", "get_font_size");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "font_color"), "set_font_color", "get_font_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "shadow_color"), "set_shadow_color", "get_shadow_color");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "horizontal_alignment", PROPERTY_HINT_ENUM, "Left,Center,Right"), "set_horizontal_alignment", "get_horizontal_alignment");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "vertical_alignment", PROPERTY_HINT_ENUM, "Top,Center,Bottom"), "set_vertical_alignment", "get_vertical_alignment");

	BIND_ENUM_CONSTANT(ALIGN_LEFT);
	BIND_ENUM_CONSTANT(ALIGN_CENTER);
	BIND_ENUM_CONSTANT(ALIGN_RIGHT);
	BIND_ENUM_CONSTANT(VALIGN_TOP);
	BIND_ENUM_CONSTANT(VALIGN_CENTER);
	BIND_ENUM_CONSTANT(VALIGN_BOTTOM);
}
