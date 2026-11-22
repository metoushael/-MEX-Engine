#include "theme.h"

void Theme::set_item(const String &p_name, const String &p_type, const Variant &p_value) {
	items[p_type + "/" + p_name] = p_value;
	emit_changed();
}

Variant Theme::get_item(const String &p_name, const String &p_type) const {
	return items.get(p_type + "/" + p_name, Variant());
}

bool Theme::has_item(const String &p_name, const String &p_type) const {
	return items.has(p_type + "/" + p_name);
}

void Theme::clear_item(const String &p_name, const String &p_type) {
	items.erase(p_type + "/" + p_name);
	emit_changed();
}

void Theme::set_items(const Dictionary &p_items) {
	items = p_items.duplicate();
	emit_changed();
}

void Theme::set_default_font(const Ref<Font> &p_font) {
	default_font = p_font;
	emit_changed();
}

void Theme::set_default_font_size(int p_size) {
	default_font_size = std::max(0, p_size);
	emit_changed();
}

void Theme::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_item", "name", "type", "value"), &Theme::set_item);
	ClassDB::bind_method(D_METHOD("get_item", "name", "type"), &Theme::get_item);
	ClassDB::bind_method(D_METHOD("has_item", "name", "type"), &Theme::has_item);
	ClassDB::bind_method(D_METHOD("clear_item", "name", "type"), &Theme::clear_item);
	ClassDB::bind_method(D_METHOD("set_color", "name", "type", "color"), &Theme::set_color);
	ClassDB::bind_method(D_METHOD("get_color", "name", "type"), &Theme::get_color);
	ClassDB::bind_method(D_METHOD("has_color", "name", "type"), &Theme::has_color);
	ClassDB::bind_method(D_METHOD("set_constant", "name", "type", "value"), &Theme::set_constant);
	ClassDB::bind_method(D_METHOD("get_constant", "name", "type"), &Theme::get_constant);
	ClassDB::bind_method(D_METHOD("set_font", "name", "type", "font"), &Theme::set_font);
	ClassDB::bind_method(D_METHOD("set_font_size", "name", "type", "size"), &Theme::set_font_size);
	ClassDB::bind_method(D_METHOD("set_items", "items"), &Theme::set_items);
	ClassDB::bind_method(D_METHOD("get_items"), &Theme::get_items);
	ClassDB::bind_method(D_METHOD("set_default_font", "font"), &Theme::set_default_font);
	ClassDB::bind_method(D_METHOD("get_default_font"), &Theme::get_default_font);
	ClassDB::bind_method(D_METHOD("set_default_font_size", "size"), &Theme::set_default_font_size);
	ClassDB::bind_method(D_METHOD("get_default_font_size"), &Theme::get_default_font_size);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "default_font", PROPERTY_HINT_RESOURCE_TYPE, "Font"), "set_default_font", "get_default_font");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "default_font_size", PROPERTY_HINT_RANGE, "0,256,1"), "set_default_font_size", "get_default_font_size");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "items"), "set_items", "get_items");
}
