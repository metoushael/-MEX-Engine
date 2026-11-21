// Button : bouton cliquable émettant le signal "pressed".
#pragma once

#include "scene/gui/control.h"

class Button : public Control {
	GDCLASS(Button, Control)

	String text = "Button";
	Ref<Font> font;
	int font_size = 16;
	bool disabled = false;
	bool toggle_mode = false;
	bool toggled_on = false;
	Color color_normal = Color(0.20f, 0.23f, 0.29f);
	Color color_hover = Color(0.27f, 0.31f, 0.39f);
	Color color_pressed = Color(0.16f, 0.42f, 0.72f);
	Color font_color = Color(0.93f, 0.94f, 0.96f);

	bool hovered = false;
	bool pressing = false;

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void input(const Ref<InputEvent> &p_event) override;

public:
	void set_text(const String &p_text);
	String get_text() const { return text; }
	void set_font(const Ref<Font> &p_font);
	Ref<Font> get_font() const { return font; }
	void set_font_size(int p_size);
	int get_font_size() const { return font_size; }
	void set_disabled(bool p_disabled);
	bool is_disabled() const { return disabled; }
	void set_toggle_mode(bool p_enabled) { toggle_mode = p_enabled; }
	bool is_toggle_mode() const { return toggle_mode; }
	void set_pressed(bool p_pressed);
	bool is_pressed() const { return toggle_mode ? toggled_on : pressing; }
	bool is_hovered() const { return hovered; }
	void set_color_normal(const Color &p_color);
	Color get_color_normal() const { return color_normal; }
	void set_color_hover(const Color &p_color);
	Color get_color_hover() const { return color_hover; }
	void set_color_pressed(const Color &p_color);
	Color get_color_pressed() const { return color_pressed; }
	void set_font_color(const Color &p_color);
	Color get_font_color() const { return font_color; }

	virtual Size2 get_minimum_size() const override;

	Button();
};
