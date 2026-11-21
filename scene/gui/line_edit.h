// LineEdit : champ de saisie de texte sur une ligne.
#pragma once

#include "scene/gui/control.h"
#include "scene/resources/font.h"

class LineEdit : public Control {
	GDCLASS(LineEdit, Control)

	String text;
	String placeholder_text;
	int max_length = 0; // 0 = illimité (en caractères)
	bool editable = true;
	int font_size = 16;
	Color font_color = Color(1, 1, 1);
	Color bg_color = Color(0.10f, 0.11f, 0.14f, 0.9f);
	bool focused = false;
	int caret = 0; // position en octets dans `text` (toujours sur une frontière UTF-8)
	double blink_time = 0;
	static LineEdit *focus_owner;

	int _prev_char(int p_pos) const;
	int _next_char(int p_pos) const;
	int _char_count() const;
	void _set_text_internal(const String &p_text);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual void input(const Ref<InputEvent> &p_event) override;
	virtual Size2 get_minimum_size() const override;

	void set_text(const String &p_text);
	String get_text() const { return text; }
	void set_placeholder(const String &p_text);
	String get_placeholder() const { return placeholder_text; }
	void set_max_length(int p_length) { max_length = std::max(0, p_length); }
	int get_max_length() const { return max_length; }
	void set_editable(bool p_editable);
	bool is_editable() const { return editable; }
	void set_font_size(int p_size);
	int get_font_size() const { return font_size; }
	void set_font_color(const Color &p_color);
	Color get_font_color() const { return font_color; }
	void set_bg_color(const Color &p_color);
	Color get_bg_color() const { return bg_color; }

	void grab_focus();
	void release_focus();
	bool has_focus() const { return focused; }
	void clear() { set_text(""); }

	LineEdit();
	~LineEdit() {
		if (focus_owner == this) {
			focus_owner = nullptr;
		}
	}
};
