#include "panel.h"

#include "core/string/ustring.h"

// --- ColorRect ---
void ColorRect::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW) {
		draw_rect(Rect2(Vector2(), get_size()), color, true);
	}
}

void ColorRect::set_color(const Color &p_color) {
	color = p_color;
	queue_redraw();
}

void ColorRect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_color", "color"), &ColorRect::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &ColorRect::get_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
}

// --- Panel ---
void Panel::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW) {
		const Color bg_color = get_theme_color("bg_color", this->bg_color);
		const Color border_color = get_theme_color("border_color", this->border_color);
		draw_rect(Rect2(Vector2(), get_size()), bg_color, true);
		if (border_width > 0 && border_color.a > 0) {
			draw_rect(Rect2(Vector2(), get_size()), border_color, false, border_width);
		}
	}
}

void Panel::set_bg_color(const Color &p_color) {
	bg_color = p_color;
	queue_redraw();
}

void Panel::set_border_color(const Color &p_color) {
	border_color = p_color;
	queue_redraw();
}

void Panel::set_border_width(real_t p_width) {
	border_width = std::max(0.0f, p_width);
	queue_redraw();
}

void Panel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bg_color", "color"), &Panel::set_bg_color);
	ClassDB::bind_method(D_METHOD("get_bg_color"), &Panel::get_bg_color);
	ClassDB::bind_method(D_METHOD("set_border_color", "color"), &Panel::set_border_color);
	ClassDB::bind_method(D_METHOD("get_border_color"), &Panel::get_border_color);
	ClassDB::bind_method(D_METHOD("set_border_width", "width"), &Panel::set_border_width);
	ClassDB::bind_method(D_METHOD("get_border_width"), &Panel::get_border_width);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bg_color"), "set_bg_color", "get_bg_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "border_color"), "set_border_color", "get_border_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "border_width", PROPERTY_HINT_RANGE, "0,16,0.5"), "set_border_width", "get_border_width");
}

// --- TextureRect ---
void TextureRect::_notification(int p_what) {
	if (p_what != NOTIFICATION_DRAW || texture.is_null()) {
		return;
	}
	const Size2 size = get_size();
	const Size2 tex_size = texture->get_size();
	if (tex_size.x <= 0 || tex_size.y <= 0) {
		return;
	}
	if (stretch_mode == STRETCH_TILE) {
		for (real_t y = 0; y < size.y; y += tex_size.y) {
			for (real_t x = 0; x < size.x; x += tex_size.x) {
				Size2 part(std::min(tex_size.x, size.x - x), std::min(tex_size.y, size.y - y));
				draw_texture_rect_region(texture, Rect2(Vector2(x, y), part), Rect2(Vector2(), part));
			}
		}
		return;
	}
	Rect2 dst;
	switch (stretch_mode) {
		case STRETCH_KEEP:
			dst = Rect2(Vector2(), tex_size);
			break;
		case STRETCH_KEEP_CENTERED:
			dst = Rect2(((size - tex_size) * 0.5f).round(), tex_size);
			break;
		case STRETCH_KEEP_ASPECT_CENTERED: {
			real_t scale = std::min(size.x / tex_size.x, size.y / tex_size.y);
			Size2 scaled = tex_size * scale;
			dst = Rect2((size - scaled) * 0.5f, scaled);
		} break;
		default:
			dst = Rect2(Vector2(), size);
			break;
	}
	if (flip_h) {
		dst.position.x += dst.size.x;
		dst.size.x = -dst.size.x;
	}
	if (flip_v) {
		dst.position.y += dst.size.y;
		dst.size.y = -dst.size.y;
	}
	draw_texture_rect(texture, dst);
}

void TextureRect::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	queue_redraw();
}

void TextureRect::set_stretch_mode(StretchMode p_mode) {
	stretch_mode = p_mode;
	queue_redraw();
}

void TextureRect::set_flip_h(bool p_flip) {
	flip_h = p_flip;
	queue_redraw();
}

void TextureRect::set_flip_v(bool p_flip) {
	flip_v = p_flip;
	queue_redraw();
}

void TextureRect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &TextureRect::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &TextureRect::get_texture);
	ClassDB::bind_method(D_METHOD("set_stretch_mode", "mode"), &TextureRect::set_stretch_mode);
	ClassDB::bind_method(D_METHOD("get_stretch_mode"), &TextureRect::get_stretch_mode);
	ClassDB::bind_method(D_METHOD("set_flip_h", "flip"), &TextureRect::set_flip_h);
	ClassDB::bind_method(D_METHOD("is_flipped_h"), &TextureRect::is_flipped_h);
	ClassDB::bind_method(D_METHOD("set_flip_v", "flip"), &TextureRect::set_flip_v);
	ClassDB::bind_method(D_METHOD("is_flipped_v"), &TextureRect::is_flipped_v);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "stretch_mode", PROPERTY_HINT_ENUM, "Scale,Keep,Keep Centered,Keep Aspect Centered,Tile"), "set_stretch_mode", "get_stretch_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_h"), "set_flip_h", "is_flipped_h");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_v"), "set_flip_v", "is_flipped_v");
	BIND_ENUM_CONSTANT(STRETCH_SCALE);
	BIND_ENUM_CONSTANT(STRETCH_KEEP);
	BIND_ENUM_CONSTANT(STRETCH_KEEP_CENTERED);
	BIND_ENUM_CONSTANT(STRETCH_KEEP_ASPECT_CENTERED);
	BIND_ENUM_CONSTANT(STRETCH_TILE);
}

// --- ProgressBar ---
ProgressBar::ProgressBar() {
	set_size(Vector2(200, 22));
}

double ProgressBar::get_ratio() const {
	if (max_value <= min_value) {
		return 0;
	}
	return Math::clamp((value - min_value) / (max_value - min_value), 0.0, 1.0);
}

void ProgressBar::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW) {
		const Size2 size = get_size();
		draw_rect(Rect2(Vector2(), size), bg_color, true);
		const real_t ratio = (real_t)get_ratio();
		if (ratio > 0) {
			draw_rect(Rect2(Vector2(), Size2(size.x * ratio, size.y)), fill_color, true);
		}
		draw_rect(Rect2(Vector2(), size), Color(1, 1, 1, 0.18f), false, 1.0f);
		if (show_percentage) {
			Ref<Font> f = Font::get_default();
			const int font_size = Math::clamp((int)(size.y * 0.7f), 8, 20);
			String label = itos((int64_t)std::round(ratio * 100.0f)) + " %";
			Size2 text_size = f->get_string_size(label, font_size);
			draw_string(f, ((size - text_size) * 0.5f).round(), label, font_size, Color(1, 1, 1, 0.95f));
		}
	}
}

void ProgressBar::set_min(double p_min) {
	min_value = p_min;
	queue_redraw();
}

void ProgressBar::set_max(double p_max) {
	max_value = p_max;
	queue_redraw();
}

void ProgressBar::set_value(double p_value) {
	double v = Math::clamp(p_value, std::min(min_value, max_value), std::max(min_value, max_value));
	if (v == value) {
		return;
	}
	value = v;
	queue_redraw();
	emit_signal("value_changed", value);
}

void ProgressBar::set_show_percentage(bool p_show) {
	show_percentage = p_show;
	queue_redraw();
}

void ProgressBar::set_bg_color(const Color &p_color) {
	bg_color = p_color;
	queue_redraw();
}

void ProgressBar::set_fill_color(const Color &p_color) {
	fill_color = p_color;
	queue_redraw();
}

void ProgressBar::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_min", "min"), &ProgressBar::set_min);
	ClassDB::bind_method(D_METHOD("get_min"), &ProgressBar::get_min);
	ClassDB::bind_method(D_METHOD("set_max", "max"), &ProgressBar::set_max);
	ClassDB::bind_method(D_METHOD("get_max"), &ProgressBar::get_max);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &ProgressBar::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &ProgressBar::get_value);
	ClassDB::bind_method(D_METHOD("get_ratio"), &ProgressBar::get_ratio);
	ClassDB::bind_method(D_METHOD("set_show_percentage", "show"), &ProgressBar::set_show_percentage);
	ClassDB::bind_method(D_METHOD("is_percentage_shown"), &ProgressBar::is_percentage_shown);
	ClassDB::bind_method(D_METHOD("set_bg_color", "color"), &ProgressBar::set_bg_color);
	ClassDB::bind_method(D_METHOD("get_bg_color"), &ProgressBar::get_bg_color);
	ClassDB::bind_method(D_METHOD("set_fill_color", "color"), &ProgressBar::set_fill_color);
	ClassDB::bind_method(D_METHOD("get_fill_color"), &ProgressBar::get_fill_color);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_value"), "set_min", "get_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_value"), "set_max", "get_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value"), "set_value", "get_value");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_percentage"), "set_show_percentage", "is_percentage_shown");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bg_color"), "set_bg_color", "get_bg_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fill_color"), "set_fill_color", "get_fill_color");
	ADD_SIGNAL(MethodInfo("value_changed", PropertyInfo(Variant::FLOAT, "value")));
}
