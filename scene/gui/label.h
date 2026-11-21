// Label : affiche du texte.
#pragma once

#include "scene/gui/control.h"

class Label : public Control {
	GDCLASS(Label, Control)

public:
	enum HorizontalAlignment {
		ALIGN_LEFT,
		ALIGN_CENTER,
		ALIGN_RIGHT,
	};
	enum VerticalAlignment {
		VALIGN_TOP,
		VALIGN_CENTER,
		VALIGN_BOTTOM,
	};

private:
	String text;
	Ref<Font> font;
	int font_size = 16;
	Color font_color = Color(1, 1, 1, 1);
	Color shadow_color = Color(0, 0, 0, 0);
	HorizontalAlignment horizontal_alignment = ALIGN_LEFT;
	VerticalAlignment vertical_alignment = VALIGN_TOP;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_text(const String &p_text);
	String get_text() const { return text; }
	void set_font(const Ref<Font> &p_font);
	Ref<Font> get_font() const { return font; }
	void set_font_size(int p_size);
	int get_font_size() const { return font_size; }
	void set_font_color(const Color &p_color);
	Color get_font_color() const { return font_color; }
	void set_shadow_color(const Color &p_color);
	Color get_shadow_color() const { return shadow_color; }
	void set_horizontal_alignment(HorizontalAlignment p_align);
	HorizontalAlignment get_horizontal_alignment() const { return horizontal_alignment; }
	void set_vertical_alignment(VerticalAlignment p_align);
	VerticalAlignment get_vertical_alignment() const { return vertical_alignment; }

	virtual Size2 get_minimum_size() const override;
	virtual Rect2 _edit_get_rect() const override;

	Label();
};
