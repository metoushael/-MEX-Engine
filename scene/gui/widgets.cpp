#include "widgets.h"

#include "core/input/input_event.h"
#include "core/input/keyboard.h"
#include "core/string/ustring.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/scene_tree.h"

static Array _array_without(const Array &p_array, int p_index) {
	Array out;
	for (int i = 0; i < p_array.size(); i++) {
		if (i != p_index) {
			out.push_back(p_array[i]);
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// CheckBox
// ---------------------------------------------------------------------------
CheckBox::CheckBox() {
	toggle_mode = true;
	text = "CheckBox";
	set_size(Vector2(140, 28));
}

Size2 CheckBox::get_minimum_size() const {
	const int size = get_theme_font_size("font_size", font_size);
	Ref<Font> f = get_theme_font("font", font);
	const Size2 text_size = f->get_string_size(atr(text), size);
	const real_t box = std::max((real_t)14, f->get_height(size) * 0.9f);
	return Size2(box + 8 + text_size.x, std::max(box, text_size.y) + 6);
}

bool CheckBox::_draw_custom() {
	const Size2 size = get_size();
	const int text_size = get_theme_font_size("font_size", font_size);
	Ref<Font> f = get_theme_font("font", font);
	Color text_color = get_theme_color("font_color", font_color);
	const Color accent = get_theme_color("color_pressed", color_pressed);
	const real_t box = std::max((real_t)14, f->get_height(text_size) * 0.9f);
	const Rect2 box_rect(0, std::floor((size.y - box) * 0.5f), box, box);
	draw_rect(box_rect, get_theme_color("color_normal", hovered && !disabled ? color_hover : color_normal), true);
	draw_rect(box_rect, has_focus() ? get_theme_color("focus_color", Color(0.45f, 0.65f, 1.0f, 0.95f)) : Color(1, 1, 1, hovered ? 0.5f : 0.3f), false, has_focus() ? 2.0f : 1.0f);
	if (toggled_on) {
		const real_t inset = std::floor(box * 0.22f);
		draw_rect(Rect2(box_rect.position + Vector2(inset, inset), box_rect.size - Vector2(inset, inset) * 2), accent, true);
	}
	if (disabled) {
		text_color.a *= 0.5f;
	}
	draw_string(f, Vector2(box + 8, std::floor((size.y - f->get_height(text_size)) * 0.5f)), atr(text), text_size, text_color);
	return true;
}

// ---------------------------------------------------------------------------
// TextureButton
// ---------------------------------------------------------------------------
TextureButton::TextureButton() {
	text = "";
	set_size(Vector2(64, 64));
}

Size2 TextureButton::get_minimum_size() const {
	return texture_normal.is_valid() ? texture_normal->get_size() : Size2();
}

bool TextureButton::_draw_custom() {
	Ref<Texture2D> texture = texture_normal;
	Color tint(1, 1, 1, 1);
	if (disabled) {
		if (texture_disabled.is_valid()) {
			texture = texture_disabled;
		} else {
			tint = Color(1, 1, 1, 0.5f);
		}
	} else if (pressing || toggled_on) {
		if (texture_pressed.is_valid()) {
			texture = texture_pressed;
		} else {
			tint = Color(0.75f, 0.75f, 0.75f, 1); // sans image dédiée : assombri
		}
	} else if (hovered) {
		if (texture_hover.is_valid()) {
			texture = texture_hover;
		} else {
			tint = Color(1.15f, 1.15f, 1.15f, 1);
		}
	}
	if (texture.is_valid()) {
		draw_texture_rect(texture, Rect2(Vector2(), get_size()), tint);
	} else {
		draw_rect(Rect2(Vector2(), get_size()), Color(1, 1, 1, 0.25f), false, 1.0f);
	}
	if (has_focus()) {
		draw_rect(Rect2(Vector2(), get_size()), get_theme_color("focus_color", Color(0.45f, 0.65f, 1.0f, 0.95f)), false, 2.0f);
	}
	return true;
}

#define TEXTURE_BUTTON_SETTER(m_name, m_field) \
	void TextureButton::m_name(const Ref<Texture2D> &p_texture) { \
		m_field = p_texture; \
		queue_redraw(); \
		update_minimum_size(); \
	}
TEXTURE_BUTTON_SETTER(set_texture_normal, texture_normal)
TEXTURE_BUTTON_SETTER(set_texture_hover, texture_hover)
TEXTURE_BUTTON_SETTER(set_texture_pressed, texture_pressed)
TEXTURE_BUTTON_SETTER(set_texture_disabled, texture_disabled)
#undef TEXTURE_BUTTON_SETTER

void TextureButton::_bind_methods() {
#define BIND_TEXTURE(m_name) \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "texture"), &TextureButton::set_##m_name); \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &TextureButton::get_##m_name); \
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, #m_name, PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_" #m_name, "get_" #m_name)
	BIND_TEXTURE(texture_normal);
	BIND_TEXTURE(texture_hover);
	BIND_TEXTURE(texture_pressed);
	BIND_TEXTURE(texture_disabled);
#undef BIND_TEXTURE
}

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------
Slider::Slider(bool p_vertical) :
		vertical(p_vertical) {
	set_focus_mode(FOCUS_ALL);
	set_size(p_vertical ? Vector2(16, 120) : Vector2(160, 16));
}

void Slider::set_min(double p_min) {
	min_value = p_min;
	set_value(value);
	queue_redraw();
}

void Slider::set_max(double p_max) {
	max_value = p_max;
	set_value(value);
	queue_redraw();
}

void Slider::set_value(double p_value) {
	double v = p_value;
	if (step > 0) {
		v = std::round((v - min_value) / step) * step + min_value;
	}
	v = Math::clamp(v, min_value, std::max(min_value, max_value));
	if (v == value) {
		return;
	}
	value = v;
	queue_redraw();
	emit_signal("value_changed", value);
}

void Slider::_set_from_position(const Vector2 &p_local) {
	const Size2 size = get_size();
	const real_t grabber = vertical ? size.x : size.y;
	const real_t length = (vertical ? size.y : size.x) - grabber;
	real_t ratio = length > 0 ? ((vertical ? p_local.y : p_local.x) - grabber * 0.5f) / length : 0;
	ratio = Math::clamp<real_t>(vertical ? 1.0f - ratio : ratio, 0, 1); // vertical : le maximum est en haut
	set_value(min_value + (max_value - min_value) * ratio);
}

void Slider::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_input(!get_tree()->is_edit_mode());
		} break;
		case NOTIFICATION_DRAW: {
			const Size2 size = get_size();
			const real_t grabber = vertical ? size.x : size.y;
			const real_t thickness = std::max((real_t)4, std::floor(grabber * 0.35f));
			const real_t ratio = (real_t)get_ratio();
			const Color bg = get_theme_color("bg_color", bg_color);
			const Color fill = get_theme_color("fill_color", fill_color);
			const Color knob = get_theme_color("grabber_color", grabber_color);
			if (vertical) {
				const real_t x = std::floor((size.x - thickness) * 0.5f);
				const real_t center = grabber * 0.5f + (size.y - grabber) * (1.0f - ratio);
				draw_rect(Rect2(x, 0, thickness, size.y), bg, true);
				draw_rect(Rect2(x, center, thickness, size.y - center), fill, true);
				draw_rect(Rect2(0, center - grabber * 0.5f, grabber, grabber), knob, true);
			} else {
				const real_t y = std::floor((size.y - thickness) * 0.5f);
				const real_t center = grabber * 0.5f + (size.x - grabber) * ratio;
				draw_rect(Rect2(0, y, size.x, thickness), bg, true);
				draw_rect(Rect2(0, y, center, thickness), fill, true);
				draw_rect(Rect2(center - grabber * 0.5f, 0, grabber, grabber), knob, true);
			}
			if (has_focus()) {
				draw_rect(Rect2(Vector2(), size), get_theme_color("focus_color", Color(0.45f, 0.65f, 1.0f, 0.95f)), false, 1.0f);
			}
		} break;
	}
}

void Slider::input(const Ref<InputEvent> &p_event) {
	if (!editable || !is_visible_in_tree()) {
		return;
	}
	if (InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(p_event.ptr())) {
		if (mb->get_button_index() != MOUSE_BUTTON_LEFT) {
			return;
		}
		if (mb->is_pressed() && _hit(mb->get_position())) {
			dragging = true;
			grab_focus();
			_set_from_position(_event_to_local(mb->get_position()));
			get_tree()->set_input_as_handled();
		} else if (!mb->is_pressed() && dragging) {
			dragging = false;
			emit_signal("drag_ended");
		}
		return;
	}
	if (InputEventMouseMotion *motion = Object::cast_to<InputEventMouseMotion>(p_event.ptr())) {
		if (dragging) {
			_set_from_position(_event_to_local(motion->get_position()));
		}
		return;
	}
	InputEventKey *key = Object::cast_to<InputEventKey>(p_event.ptr());
	if (!key || !key->is_pressed() || !has_focus()) {
		return;
	}
	// Les flèches dans l'axe du curseur changent la valeur ; les autres déplacent le focus.
	const double amount = step > 0 ? step : (max_value - min_value) / 100.0;
	const int code = key->get_keycode();
	const int less = vertical ? KEY_DOWN : KEY_LEFT;
	const int more = vertical ? KEY_UP : KEY_RIGHT;
	if (code == less || code == more) {
		set_value(value + (code == more ? amount : -amount));
		get_tree()->set_input_as_handled();
	}
}

void Slider::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_min", "min"), &Slider::set_min);
	ClassDB::bind_method(D_METHOD("get_min"), &Slider::get_min);
	ClassDB::bind_method(D_METHOD("set_max", "max"), &Slider::set_max);
	ClassDB::bind_method(D_METHOD("get_max"), &Slider::get_max);
	ClassDB::bind_method(D_METHOD("set_step", "step"), &Slider::set_step);
	ClassDB::bind_method(D_METHOD("get_step"), &Slider::get_step);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &Slider::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &Slider::get_value);
	ClassDB::bind_method(D_METHOD("get_ratio"), &Slider::get_ratio);
	ClassDB::bind_method(D_METHOD("set_editable", "editable"), &Slider::set_editable);
	ClassDB::bind_method(D_METHOD("is_editable"), &Slider::is_editable);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_value"), "set_min", "get_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_value"), "set_max", "get_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "step"), "set_step", "get_step");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value"), "set_value", "get_value");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "editable"), "set_editable", "is_editable");
	ADD_SIGNAL(MethodInfo("value_changed", PropertyInfo(Variant::FLOAT, "value")));
	ADD_SIGNAL(MethodInfo("drag_ended"));
}

// ---------------------------------------------------------------------------
// OptionButton
// ---------------------------------------------------------------------------
OptionButton::OptionButton() {
	text = "";
	RenderingServer *rs = RenderingServer::get_singleton();
	popup_item = rs->canvas_item_create();
	rs->canvas_item_set_z_as_relative(popup_item, false);
	rs->canvas_item_set_z_index(popup_item, 4096); // par-dessus toute l'interface
	set_size(Vector2(160, 36));
}

OptionButton::~OptionButton() {
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(popup_item);
	}
}

real_t OptionButton::_row_height() const {
	return get_theme_font("font", font)->get_height(get_theme_font_size("font_size", font_size)) + 10;
}

Rect2 OptionButton::_popup_rect() const {
	return Rect2(0, get_size().y, get_size().x, _row_height() * (real_t)items.size());
}

Size2 OptionButton::get_minimum_size() const {
	Ref<Font> f = get_theme_font("font", font);
	const int size = get_theme_font_size("font_size", font_size);
	real_t width = 0;
	for (int i = 0; i < items.size(); i++) {
		width = std::max(width, f->get_string_size(atr(items[i].as_string()), size).x);
	}
	return Size2(width + 16 + 22, f->get_height(size) + 8);
}

void OptionButton::_update_popup() {
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->canvas_item_clear(popup_item);
	if (!open || !is_inside_tree()) {
		return;
	}
	rs->canvas_item_set_transform(popup_item, get_global_transform());
	Ref<Font> f = get_theme_font("font", font);
	const int size = get_theme_font_size("font_size", font_size);
	const Rect2 rect = _popup_rect();
	const real_t row = _row_height();
	rs->canvas_item_add_rect(popup_item, rect, get_theme_color("popup_color", Color(0.12f, 0.14f, 0.18f, 1)));
	for (int i = 0; i < items.size(); i++) {
		const Rect2 line(rect.position.x, rect.position.y + row * (real_t)i, rect.size.x, row);
		if (i == highlighted) {
			rs->canvas_item_add_rect(popup_item, line, get_theme_color("color_pressed", color_pressed));
		} else if (i == selected) {
			rs->canvas_item_add_rect(popup_item, line, Color(1, 1, 1, 0.08f));
		}
		f->draw_string(popup_item, line.position + Vector2(8, 5), atr(items[i].as_string()), size, get_theme_color("font_color", font_color));
	}
	const Color border(1, 1, 1, 0.3f);
	rs->canvas_item_add_rect(popup_item, Rect2(rect.position, Size2(rect.size.x, 1)), border);
	rs->canvas_item_add_rect(popup_item, Rect2(rect.position + Vector2(0, rect.size.y - 1), Size2(rect.size.x, 1)), border);
	rs->canvas_item_add_rect(popup_item, Rect2(rect.position, Size2(1, rect.size.y)), border);
	rs->canvas_item_add_rect(popup_item, Rect2(rect.position + Vector2(rect.size.x - 1, 0), Size2(1, rect.size.y)), border);
}

void OptionButton::_open(bool p_open) {
	if (open == p_open || (p_open && items.is_empty())) {
		return;
	}
	open = p_open;
	highlighted = selected;
	if (open) {
		const Rect2 rect = _popup_rect();
		const Transform2D to_screen = get_canvas_transform() * get_global_transform();
		_set_modal(true, Rect2(to_screen.xform(rect.position), rect.size * to_screen.get_scale()));
	} else {
		_set_modal(false);
	}
	_update_popup();
	queue_redraw();
}

void OptionButton::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			CanvasLayer *layer = get_canvas_layer_node();
			RenderingServer::get_singleton()->canvas_item_set_parent(popup_item, layer ? layer->get_canvas() : get_tree()->get_canvas());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_open(false);
			RenderingServer::get_singleton()->canvas_item_set_parent(popup_item, RID());
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED:
		case NOTIFICATION_FOCUS_EXIT: {
			_open(false);
		} break;
	}
}

void OptionButton::_activate() {
	_open(!open);
}

void OptionButton::_draw_extra() {
	// Petite flèche vers le bas, à droite du texte.
	const Size2 size = get_size();
	const Color arrow = get_theme_color("font_color", font_color);
	const real_t x = size.x - 18, y = std::floor(size.y * 0.5f) - 2;
	for (int i = 0; i < 5; i++) {
		draw_rect(Rect2(x + (real_t)i, y + (real_t)i, (real_t)(9 - 2 * i), 1), arrow, true);
	}
}

void OptionButton::input(const Ref<InputEvent> &p_event) {
	if (!open) {
		Button::input(p_event);
		return;
	}
	const real_t row = _row_height();
	if (InputEventMouseMotion *motion = Object::cast_to<InputEventMouseMotion>(p_event.ptr())) {
		const Vector2 local = _event_to_local(motion->get_position());
		const int over = _popup_rect().has_point(local) ? (int)((local.y - get_size().y) / row) : -1;
		if (over != highlighted && over >= 0) {
			highlighted = over;
			_update_popup();
		}
		Button::input(p_event);
		return;
	}
	if (InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(p_event.ptr())) {
		if (mb->get_button_index() != MOUSE_BUTTON_LEFT || !mb->is_pressed()) {
			Button::input(p_event);
			return;
		}
		const Vector2 local = _event_to_local(mb->get_position());
		if (_popup_rect().has_point(local)) {
			const int index = Math::clamp((int)((local.y - get_size().y) / row), 0, items.size() - 1);
			_open(false);
			if (index != selected) {
				select(index);
				emit_signal("item_selected", index);
			}
			get_tree()->set_input_as_handled();
		} else if (has_point(local)) {
			Button::input(p_event); // le relâchement refermera la liste
		} else {
			_open(false); // clic ailleurs : la liste se ferme, le clic continue son chemin
		}
		return;
	}
	InputEventKey *key = Object::cast_to<InputEventKey>(p_event.ptr());
	if (!key || !key->is_pressed()) {
		return;
	}
	const int code = key->get_keycode();
	if (code == KEY_UP || code == KEY_DOWN) {
		highlighted = Math::clamp(highlighted + (code == KEY_DOWN ? 1 : -1), 0, items.size() - 1);
		_update_popup();
	} else if (code == KEY_ENTER || code == KEY_SPACE) {
		const int index = highlighted;
		_open(false);
		if (index >= 0 && index != selected) {
			select(index);
			emit_signal("item_selected", index);
		}
	} else if (code == KEY_ESCAPE || code == KEY_TAB) {
		_open(false);
	} else {
		return;
	}
	get_tree()->set_input_as_handled();
}

void OptionButton::add_item(const String &p_text) {
	items.push_back(p_text);
	if (selected < 0) {
		select(0);
	}
	update_minimum_size();
}

void OptionButton::remove_item(int p_index) {
	ERR_FAIL_INDEX(p_index, items.size());
	items = _array_without(items, p_index);
	select(items.is_empty() ? -1 : std::min(selected, items.size() - 1));
	update_minimum_size();
}

void OptionButton::clear() {
	items = Array();
	select(-1);
	update_minimum_size();
}

String OptionButton::get_item_text(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, items.size(), String());
	return items[p_index].as_string();
}

void OptionButton::set_item_text(int p_index, const String &p_text) {
	ERR_FAIL_INDEX(p_index, items.size());
	items[p_index] = p_text;
	select(selected);
}

void OptionButton::set_items(const Array &p_items) {
	items = p_items.duplicate();
	select(items.is_empty() ? -1 : Math::clamp(selected, 0, items.size() - 1));
	update_minimum_size();
}

void OptionButton::select(int p_index) {
	selected = p_index >= 0 && p_index < items.size() ? p_index : -1;
	text = selected >= 0 ? items[selected].as_string() : String();
	queue_redraw();
	_update_popup();
}

void OptionButton::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_item", "text"), &OptionButton::add_item);
	ClassDB::bind_method(D_METHOD("remove_item", "index"), &OptionButton::remove_item);
	ClassDB::bind_method(D_METHOD("clear"), &OptionButton::clear);
	ClassDB::bind_method(D_METHOD("get_item_count"), &OptionButton::get_item_count);
	ClassDB::bind_method(D_METHOD("get_item_text", "index"), &OptionButton::get_item_text);
	ClassDB::bind_method(D_METHOD("set_item_text", "index", "text"), &OptionButton::set_item_text);
	ClassDB::bind_method(D_METHOD("set_items", "items"), &OptionButton::set_items);
	ClassDB::bind_method(D_METHOD("get_items"), &OptionButton::get_items);
	ClassDB::bind_method(D_METHOD("select", "index"), &OptionButton::select);
	ClassDB::bind_method(D_METHOD("get_selected"), &OptionButton::get_selected);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "items"), "set_items", "get_items");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "selected"), "select", "get_selected");
	ADD_SIGNAL(MethodInfo("item_selected", PropertyInfo(Variant::INT, "index")));
}

// ---------------------------------------------------------------------------
// ItemList
// ---------------------------------------------------------------------------
ItemList::ItemList() {
	set_focus_mode(FOCUS_ALL);
	set_clip_contents(true);
	set_size(Vector2(200, 140));
}

real_t ItemList::_row_height() const {
	return get_theme_font("font", Ref<Font>())->get_height(get_theme_font_size("font_size", font_size)) + 8;
}

real_t ItemList::_max_scroll() const {
	return std::max((real_t)0, _row_height() * (real_t)items.size() - get_size().y);
}

void ItemList::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_input(!get_tree()->is_edit_mode());
		} break;
		case NOTIFICATION_RESIZED: {
			scroll = std::min(scroll, _max_scroll());
		} break;
		case NOTIFICATION_DRAW: {
			const Size2 size = get_size();
			Ref<Font> f = get_theme_font("font", Ref<Font>());
			const int text_size = get_theme_font_size("font_size", font_size);
			const real_t row = _row_height();
			draw_rect(Rect2(Vector2(), size), get_theme_color("bg_color", bg_color), true);
			for (int i = 0; i < items.size(); i++) {
				const real_t y = row * (real_t)i - scroll;
				if (y + row < 0 || y > size.y) {
					continue;
				}
				if (i == selected) {
					draw_rect(Rect2(0, y, size.x, row), get_theme_color("selection_color", selection_color), true);
				}
				draw_string(f, Vector2(8, y + 4), atr(items[i].as_string()), text_size, get_theme_color("font_color", font_color));
			}
			const real_t max_scroll = _max_scroll();
			if (max_scroll > 0) {
				const real_t length = std::max((real_t)20, size.y * size.y / (size.y + max_scroll));
				draw_rect(Rect2(size.x - 6, (size.y - length) * (scroll / max_scroll), 6, length), Color(1, 1, 1, 0.4f), true);
			}
			draw_rect(Rect2(Vector2(), size), has_focus() ? get_theme_color("focus_color", Color(0.45f, 0.65f, 1.0f, 0.95f)) : Color(1, 1, 1, 0.2f), false, 1.0f);
		} break;
	}
}

void ItemList::input(const Ref<InputEvent> &p_event) {
	if (!is_visible_in_tree()) {
		return;
	}
	if (InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(p_event.ptr())) {
		if (!mb->is_pressed() || !_hit(mb->get_position())) {
			return;
		}
		const int button = mb->get_button_index();
		if (button == MOUSE_BUTTON_WHEEL_UP || button == MOUSE_BUTTON_WHEEL_DOWN) {
			const real_t before = scroll;
			scroll = Math::clamp<real_t>(scroll + (button == MOUSE_BUTTON_WHEEL_UP ? -1 : 1) * _row_height() * 2, 0, _max_scroll());
			if (scroll != before) {
				queue_redraw();
				get_tree()->set_input_as_handled();
			}
		} else if (button == MOUSE_BUTTON_LEFT) {
			grab_focus();
			const int index = (int)((_event_to_local(mb->get_position()).y + scroll) / _row_height());
			get_tree()->set_input_as_handled();
			if (index >= 0 && index < items.size()) {
				if (index != selected) {
					select(index);
					emit_signal("item_selected", index);
				}
				if (mb->is_double_click()) {
					emit_signal("item_activated", index);
				}
			}
		}
		return;
	}
	InputEventKey *key = Object::cast_to<InputEventKey>(p_event.ptr());
	if (!key || !key->is_pressed() || !has_focus() || items.is_empty()) {
		return;
	}
	const int code = key->get_keycode();
	if (code == KEY_UP || code == KEY_DOWN) {
		const int index = Math::clamp(selected + (code == KEY_DOWN ? 1 : -1), 0, items.size() - 1);
		if (index == selected) {
			return; // en bout de liste : la flèche déplace le focus
		}
		select(index);
		ensure_current_is_visible();
		emit_signal("item_selected", index);
		get_tree()->set_input_as_handled();
	} else if (code == KEY_ENTER && selected >= 0) {
		emit_signal("item_activated", selected);
		get_tree()->set_input_as_handled();
	}
}

void ItemList::add_item(const String &p_text) {
	items.push_back(p_text);
	queue_redraw();
}

void ItemList::remove_item(int p_index) {
	ERR_FAIL_INDEX(p_index, items.size());
	items = _array_without(items, p_index);
	selected = selected == p_index ? -1 : (selected > p_index ? selected - 1 : selected);
	scroll = std::min(scroll, _max_scroll());
	queue_redraw();
}

void ItemList::clear() {
	items = Array();
	selected = -1;
	scroll = 0;
	queue_redraw();
}

String ItemList::get_item_text(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, items.size(), String());
	return items[p_index].as_string();
}

void ItemList::set_item_text(int p_index, const String &p_text) {
	ERR_FAIL_INDEX(p_index, items.size());
	items[p_index] = p_text;
	queue_redraw();
}

void ItemList::set_items(const Array &p_items) {
	items = p_items.duplicate();
	selected = selected < items.size() ? selected : -1;
	queue_redraw();
}

void ItemList::select(int p_index) {
	selected = p_index >= 0 && p_index < items.size() ? p_index : -1;
	queue_redraw();
}

void ItemList::deselect_all() {
	selected = -1;
	queue_redraw();
}

void ItemList::ensure_current_is_visible() {
	if (selected < 0) {
		return;
	}
	const real_t row = _row_height();
	const real_t top = row * (real_t)selected;
	if (top < scroll) {
		scroll = top;
	} else if (top + row > scroll + get_size().y) {
		scroll = top + row - get_size().y;
	}
	queue_redraw();
}

void ItemList::set_font_size(int p_size) {
	font_size = Math::clamp(p_size, 4, 256);
	queue_redraw();
}

void ItemList::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_item", "text"), &ItemList::add_item);
	ClassDB::bind_method(D_METHOD("remove_item", "index"), &ItemList::remove_item);
	ClassDB::bind_method(D_METHOD("clear"), &ItemList::clear);
	ClassDB::bind_method(D_METHOD("get_item_count"), &ItemList::get_item_count);
	ClassDB::bind_method(D_METHOD("get_item_text", "index"), &ItemList::get_item_text);
	ClassDB::bind_method(D_METHOD("set_item_text", "index", "text"), &ItemList::set_item_text);
	ClassDB::bind_method(D_METHOD("set_items", "items"), &ItemList::set_items);
	ClassDB::bind_method(D_METHOD("get_items"), &ItemList::get_items);
	ClassDB::bind_method(D_METHOD("select", "index"), &ItemList::select);
	ClassDB::bind_method(D_METHOD("deselect_all"), &ItemList::deselect_all);
	ClassDB::bind_method(D_METHOD("get_selected"), &ItemList::get_selected);
	ClassDB::bind_method(D_METHOD("ensure_current_is_visible"), &ItemList::ensure_current_is_visible);
	ClassDB::bind_method(D_METHOD("set_font_size", "size"), &ItemList::set_font_size);
	ClassDB::bind_method(D_METHOD("get_font_size"), &ItemList::get_font_size);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "items"), "set_items", "get_items");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "font_size", PROPERTY_HINT_RANGE, "4,256,1"), "set_font_size", "get_font_size");
	ADD_SIGNAL(MethodInfo("item_selected", PropertyInfo(Variant::INT, "index")));
	ADD_SIGNAL(MethodInfo("item_activated", PropertyInfo(Variant::INT, "index")));
}

// ---------------------------------------------------------------------------
// TabContainer
// ---------------------------------------------------------------------------
std::vector<Control *> TabContainer::_get_tabs() const {
	std::vector<Control *> tabs;
	for (Node *child : get_children_vector()) {
		if (Control *control = Object::cast_to<Control>(child)) {
			tabs.push_back(control);
		}
	}
	return tabs;
}

real_t TabContainer::_bar_height() const {
	return get_theme_font("font", Ref<Font>())->get_height(get_theme_font_size("font_size", font_size)) + 12;
}

String TabContainer::get_tab_title(int p_tab) const {
	const std::vector<Control *> tabs = _get_tabs();
	ERR_FAIL_INDEX_V(p_tab, tabs.size(), String());
	return titles.has(p_tab) ? titles.get(p_tab, Variant()).as_string() : tabs[(size_t)p_tab]->get_name();
}

void TabContainer::set_tab_title(int p_tab, const String &p_title) {
	titles[p_tab] = p_title;
	queue_redraw();
}

std::vector<Rect2> TabContainer::_get_tab_rects() const {
	std::vector<Rect2> rects;
	Ref<Font> f = get_theme_font("font", Ref<Font>());
	const int text_size = get_theme_font_size("font_size", font_size);
	const real_t height = _bar_height();
	real_t x = 0;
	for (int i = 0; i < get_tab_count(); i++) {
		const real_t width = f->get_string_size(atr(get_tab_title(i)), text_size).x + 24;
		rects.push_back(Rect2(x, 0, width, height));
		x += width + 2;
	}
	return rects;
}

Size2 TabContainer::get_minimum_size() const {
	Size2 min;
	for (Control *tab : _get_tabs()) {
		const Size2 tab_min = tab->get_combined_minimum_size();
		min.x = std::max(min.x, tab_min.x);
		min.y = std::max(min.y, tab_min.y);
	}
	const std::vector<Rect2> rects = _get_tab_rects();
	if (!rects.empty()) {
		min.x = std::max(min.x, rects.back().position.x + rects.back().size.x);
	}
	return min + Size2(0, _bar_height());
}

void TabContainer::_sort_children() {
	const std::vector<Control *> tabs = _get_tabs();
	current = tabs.empty() ? 0 : Math::clamp(current, 0, (int)tabs.size() - 1);
	const real_t bar = _bar_height();
	for (size_t i = 0; i < tabs.size(); i++) {
		const bool shown = (int)i == current;
		if (tabs[i]->is_visible() != shown) {
			tabs[i]->set_visible(shown);
		}
		if (shown) {
			tabs[i]->set_rect_in_container(Rect2(0, bar, get_size().x, std::max((real_t)0, get_size().y - bar)));
		}
	}
	queue_redraw();
}

void TabContainer::set_current_tab(int p_tab) {
	const int count = get_tab_count();
	const int tab = count > 0 ? Math::clamp(p_tab, 0, count - 1) : 0;
	if (tab == current) {
		return;
	}
	current = tab;
	queue_sort();
	emit_signal("tab_changed", current);
}

void TabContainer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_input(!get_tree()->is_edit_mode());
		} break;
		case NOTIFICATION_DRAW: {
			Ref<Font> f = get_theme_font("font", Ref<Font>());
			const int text_size = get_theme_font_size("font_size", font_size);
			const real_t bar = _bar_height();
			const Size2 size = get_size();
			draw_rect(Rect2(0, bar, size.x, size.y - bar), get_theme_color("bg_color", Color(0.13f, 0.15f, 0.19f, 0.92f)), true);
			const std::vector<Rect2> rects = _get_tab_rects();
			for (size_t i = 0; i < rects.size(); i++) {
				const bool active = (int)i == current;
				Color tab_color = active ? get_theme_color("bg_color", Color(0.13f, 0.15f, 0.19f, 0.92f)) : get_theme_color("tab_color", Color(0.08f, 0.09f, 0.12f, 0.92f));
				if (!active && (int)i == hovered_tab) {
					tab_color = Color(tab_color.r + 0.06f, tab_color.g + 0.06f, tab_color.b + 0.06f, tab_color.a);
				}
				draw_rect(rects[i], tab_color, true);
				if (active) {
					draw_rect(Rect2(rects[i].position, Size2(rects[i].size.x, 2)), get_theme_color("accent_color", Color(0.16f, 0.42f, 0.72f)), true);
				}
				Color text_color = get_theme_color("font_color", Color(0.93f, 0.94f, 0.96f));
				text_color.a *= active ? 1.0f : 0.65f;
				draw_string(f, rects[i].position + Vector2(12, 6), atr(get_tab_title((int)i)), text_size, text_color);
			}
		} break;
	}
}

void TabContainer::input(const Ref<InputEvent> &p_event) {
	if (!is_visible_in_tree()) {
		return;
	}
	const InputEventMouse *mouse = Object::cast_to<InputEventMouse>(p_event.ptr());
	if (!mouse) {
		return;
	}
	int over = -1;
	if (_hit(mouse->get_position())) {
		const Vector2 local = _event_to_local(mouse->get_position());
		const std::vector<Rect2> rects = _get_tab_rects();
		for (size_t i = 0; i < rects.size(); i++) {
			if (rects[i].has_point(local)) {
				over = (int)i;
			}
		}
	}
	if (over != hovered_tab) {
		hovered_tab = over;
		queue_redraw();
	}
	const InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(p_event.ptr());
	if (mb && mb->is_pressed() && mb->get_button_index() == MOUSE_BUTTON_LEFT && over >= 0) {
		set_current_tab(over);
		get_tree()->set_input_as_handled();
	}
}

void TabContainer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_current_tab", "tab"), &TabContainer::set_current_tab);
	ClassDB::bind_method(D_METHOD("get_current_tab"), &TabContainer::get_current_tab);
	ClassDB::bind_method(D_METHOD("get_tab_count"), &TabContainer::get_tab_count);
	ClassDB::bind_method(D_METHOD("set_tab_title", "tab", "title"), &TabContainer::set_tab_title);
	ClassDB::bind_method(D_METHOD("get_tab_title", "tab"), &TabContainer::get_tab_title);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "current_tab"), "set_current_tab", "get_current_tab");
	ADD_SIGNAL(MethodInfo("tab_changed", PropertyInfo(Variant::INT, "tab")));
}

// ---------------------------------------------------------------------------
// GridContainer
// ---------------------------------------------------------------------------
void GridContainer::_measure(const std::vector<Control *> &p_children, std::vector<real_t> &r_widths, std::vector<real_t> &r_heights, std::vector<bool> &r_expand_columns, std::vector<bool> &r_expand_rows) const {
	const int cols = std::max(1, columns);
	const int rows = ((int)p_children.size() + cols - 1) / cols;
	r_widths.assign((size_t)cols, 0);
	r_heights.assign((size_t)rows, 0);
	r_expand_columns.assign((size_t)cols, false);
	r_expand_rows.assign((size_t)rows, false);
	for (size_t i = 0; i < p_children.size(); i++) {
		const size_t col = i % (size_t)cols, row = i / (size_t)cols;
		const Size2 min = p_children[i]->get_combined_minimum_size();
		r_widths[col] = std::max(r_widths[col], min.x);
		r_heights[row] = std::max(r_heights[row], min.y);
		r_expand_columns[col] = r_expand_columns[col] || p_children[i]->get_h_size_flags() == SIZE_EXPAND_FILL;
		r_expand_rows[row] = r_expand_rows[row] || p_children[i]->get_v_size_flags() == SIZE_EXPAND_FILL;
	}
}

Size2 GridContainer::get_minimum_size() const {
	std::vector<real_t> widths, heights;
	std::vector<bool> expand_columns, expand_rows;
	_measure(_get_layout_children(), widths, heights, expand_columns, expand_rows);
	Size2 min;
	for (real_t w : widths) {
		min.x += w;
	}
	for (real_t h : heights) {
		min.y += h;
	}
	min.x += (real_t)h_separation * (real_t)std::max((int)widths.size() - 1, 0);
	min.y += (real_t)v_separation * (real_t)std::max((int)heights.size() - 1, 0);
	return min;
}

void GridContainer::_sort_children() {
	const std::vector<Control *> children = _get_layout_children();
	std::vector<real_t> widths, heights;
	std::vector<bool> expand_columns, expand_rows;
	_measure(children, widths, heights, expand_columns, expand_rows);
	// La place restante se partage entre les colonnes et les lignes qui demandent à s'étendre.
	auto distribute = [](std::vector<real_t> &r_sizes, const std::vector<bool> &p_expand, real_t p_available, int p_separation) {
		real_t used = (real_t)p_separation * (real_t)std::max((int)r_sizes.size() - 1, 0);
		int expanding = 0;
		for (size_t i = 0; i < r_sizes.size(); i++) {
			used += r_sizes[i];
			expanding += p_expand[i] ? 1 : 0;
		}
		if (expanding > 0 && p_available > used) {
			const real_t extra = std::floor((p_available - used) / (real_t)expanding);
			for (size_t i = 0; i < r_sizes.size(); i++) {
				if (p_expand[i]) {
					r_sizes[i] += extra;
				}
			}
		}
	};
	distribute(widths, expand_columns, get_size().x, h_separation);
	distribute(heights, expand_rows, get_size().y, v_separation);
	const size_t cols = widths.size();
	real_t y = 0;
	for (size_t row = 0; row < heights.size(); row++) {
		real_t x = 0;
		for (size_t col = 0; col < cols; col++) {
			const size_t index = row * cols + col;
			if (index < children.size()) {
				_fit_child_in_rect(children[index], Rect2(x, y, widths[col], heights[row]));
			}
			x += widths[col] + (real_t)h_separation;
		}
		y += heights[row] + (real_t)v_separation;
	}
}

void GridContainer::set_columns(int p_columns) {
	columns = std::max(1, p_columns);
	queue_sort();
}

void GridContainer::set_h_separation(int p_separation) {
	h_separation = p_separation;
	queue_sort();
}

void GridContainer::set_v_separation(int p_separation) {
	v_separation = p_separation;
	queue_sort();
}

void GridContainer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_columns", "columns"), &GridContainer::set_columns);
	ClassDB::bind_method(D_METHOD("get_columns"), &GridContainer::get_columns);
	ClassDB::bind_method(D_METHOD("set_h_separation", "separation"), &GridContainer::set_h_separation);
	ClassDB::bind_method(D_METHOD("get_h_separation"), &GridContainer::get_h_separation);
	ClassDB::bind_method(D_METHOD("set_v_separation", "separation"), &GridContainer::set_v_separation);
	ClassDB::bind_method(D_METHOD("get_v_separation"), &GridContainer::get_v_separation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "columns", PROPERTY_HINT_RANGE, "1,64,1"), "set_columns", "get_columns");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "h_separation", PROPERTY_HINT_RANGE, "0,256,1"), "set_h_separation", "get_h_separation");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "v_separation", PROPERTY_HINT_RANGE, "0,256,1"), "set_v_separation", "get_v_separation");
}

// ---------------------------------------------------------------------------
// NinePatchRect
// ---------------------------------------------------------------------------
void NinePatchRect::_notification(int p_what) {
	if (p_what != NOTIFICATION_DRAW || texture.is_null()) {
		return;
	}
	const Size2 tex = texture->get_size();
	const Size2 size = get_size();
	// Colonnes et lignes : dans la texture (source) et dans le contrôle (destination).
	const real_t sx[4] = { 0, (real_t)margins[SIDE_LEFT], tex.x - (real_t)margins[SIDE_RIGHT], tex.x };
	const real_t sy[4] = { 0, (real_t)margins[SIDE_TOP], tex.y - (real_t)margins[SIDE_BOTTOM], tex.y };
	const real_t dx[4] = { 0, (real_t)margins[SIDE_LEFT], std::max((real_t)margins[SIDE_LEFT], size.x - (real_t)margins[SIDE_RIGHT]), size.x };
	const real_t dy[4] = { 0, (real_t)margins[SIDE_TOP], std::max((real_t)margins[SIDE_TOP], size.y - (real_t)margins[SIDE_BOTTOM]), size.y };
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 3; col++) {
			if (row == 1 && col == 1 && !draw_center) {
				continue;
			}
			const Rect2 src(sx[col], sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row]);
			const Rect2 dst(dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row]);
			if (src.size.x > 0 && src.size.y > 0 && dst.size.x > 0 && dst.size.y > 0) {
				draw_texture_rect_region(texture, dst, src);
			}
		}
	}
}

void NinePatchRect::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	queue_redraw();
}

void NinePatchRect::set_patch_margin(int p_side, int p_margin) {
	margins[p_side] = std::max(0, p_margin);
	queue_redraw();
	update_minimum_size();
}

void NinePatchRect::set_draw_center(bool p_draw) {
	draw_center = p_draw;
	queue_redraw();
}

void NinePatchRect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &NinePatchRect::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &NinePatchRect::get_texture);
	ClassDB::bind_method(D_METHOD("set_draw_center", "draw"), &NinePatchRect::set_draw_center);
	ClassDB::bind_method(D_METHOD("is_draw_center_enabled"), &NinePatchRect::is_draw_center_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "draw_center"), "set_draw_center", "is_draw_center_enabled");
#define BIND_MARGIN(m_side) \
	ClassDB::bind_method(D_METHOD("set_patch_margin_" #m_side, "margin"), &NinePatchRect::set_patch_margin_##m_side); \
	ClassDB::bind_method(D_METHOD("get_patch_margin_" #m_side), &NinePatchRect::get_patch_margin_##m_side); \
	ADD_PROPERTY(PropertyInfo(Variant::INT, "patch_margin_" #m_side, PROPERTY_HINT_RANGE, "0,1024,1"), "set_patch_margin_" #m_side, "get_patch_margin_" #m_side)
	BIND_MARGIN(left);
	BIND_MARGIN(top);
	BIND_MARGIN(right);
	BIND_MARGIN(bottom);
#undef BIND_MARGIN
}

// ---------------------------------------------------------------------------
// Separator
// ---------------------------------------------------------------------------
void Separator::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW) {
		const Size2 size = get_size();
		const Color line = get_theme_color("color", color);
		if (vertical) {
			draw_rect(Rect2(std::floor(size.x * 0.5f), 0, 1, size.y), line, true);
		} else {
			draw_rect(Rect2(0, std::floor(size.y * 0.5f), size.x, 1), line, true);
		}
	}
}

void Separator::set_color(const Color &p_color) {
	color = p_color;
	queue_redraw();
}

void Separator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_color", "color"), &Separator::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &Separator::get_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
}
