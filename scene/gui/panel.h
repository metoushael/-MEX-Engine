// Contrôles décoratifs : ColorRect, Panel, TextureRect et ProgressBar.
#pragma once

#include "scene/gui/control.h"

class ColorRect : public Control {
	GDCLASS(ColorRect, Control)

	Color color = Color(1, 1, 1, 1);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_color(const Color &p_color);
	Color get_color() const { return color; }
};

class Panel : public Control {
	GDCLASS(Panel, Control)

	Color bg_color = Color(0.13f, 0.15f, 0.19f, 0.92f);
	Color border_color = Color(1, 1, 1, 0.15f);
	real_t border_width = 1.0f;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_bg_color(const Color &p_color);
	Color get_bg_color() const { return bg_color; }
	void set_border_color(const Color &p_color);
	Color get_border_color() const { return border_color; }
	void set_border_width(real_t p_width);
	real_t get_border_width() const { return border_width; }
};

class TextureRect : public Control {
	GDCLASS(TextureRect, Control)

public:
	enum StretchMode {
		STRETCH_SCALE,
		STRETCH_KEEP,
		STRETCH_KEEP_CENTERED,
		STRETCH_KEEP_ASPECT_CENTERED,
		STRETCH_TILE,
	};

private:
	Ref<Texture2D> texture;
	StretchMode stretch_mode = STRETCH_SCALE;
	bool flip_h = false;
	bool flip_v = false;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const { return texture; }
	void set_stretch_mode(StretchMode p_mode);
	StretchMode get_stretch_mode() const { return stretch_mode; }
	void set_flip_h(bool p_flip);
	bool is_flipped_h() const { return flip_h; }
	void set_flip_v(bool p_flip);
	bool is_flipped_v() const { return flip_v; }
	virtual Size2 get_minimum_size() const override { return texture.is_valid() ? texture->get_size() : Size2(); }
};

class ProgressBar : public Control {
	GDCLASS(ProgressBar, Control)

	double min_value = 0;
	double max_value = 100;
	double value = 0;
	bool show_percentage = true;
	Color bg_color = Color(0.10f, 0.11f, 0.14f, 0.9f);
	Color fill_color = Color(0.30f, 0.69f, 0.38f);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_min(double p_min);
	double get_min() const { return min_value; }
	void set_max(double p_max);
	double get_max() const { return max_value; }
	void set_value(double p_value);
	double get_value() const { return value; }
	double get_ratio() const;
	void set_show_percentage(bool p_show);
	bool is_percentage_shown() const { return show_percentage; }
	void set_bg_color(const Color &p_color);
	Color get_bg_color() const { return bg_color; }
	void set_fill_color(const Color &p_color);
	Color get_fill_color() const { return fill_color; }

	ProgressBar();
};
