// Contrôles complémentaires : CheckBox, TextureButton, Slider, OptionButton, ItemList,
// TabContainer, GridContainer, NinePatchRect, séparateurs.
#pragma once

#include "scene/gui/button.h"
#include "scene/gui/container.h"

// Case à cocher : un bouton à bascule dessiné avec une case devant son texte.
class CheckBox : public Button {
	GDCLASS(CheckBox, Button)

protected:
	static void _bind_methods() {}
	virtual bool _draw_custom() override;

public:
	virtual Size2 get_minimum_size() const override;
	CheckBox();
};

// Bouton dessiné avec des images (une par état).
class TextureButton : public Button {
	GDCLASS(TextureButton, Button)

	Ref<Texture2D> texture_normal;
	Ref<Texture2D> texture_hover;
	Ref<Texture2D> texture_pressed;
	Ref<Texture2D> texture_disabled;

protected:
	static void _bind_methods();
	virtual bool _draw_custom() override;

public:
	void set_texture_normal(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture_normal() const { return texture_normal; }
	void set_texture_hover(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture_hover() const { return texture_hover; }
	void set_texture_pressed(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture_pressed() const { return texture_pressed; }
	void set_texture_disabled(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture_disabled() const { return texture_disabled; }
	virtual Size2 get_minimum_size() const override;
	TextureButton();
};

// Curseur : choisit une valeur entre un minimum et un maximum (souris ou flèches).
class Slider : public Control {
	GDCLASS(Slider, Control)

	double min_value = 0;
	double max_value = 100;
	double step = 1;
	double value = 0;
	bool vertical = false;
	bool editable = true;
	bool dragging = false;
	Color bg_color = Color(0.10f, 0.11f, 0.14f, 0.9f);
	Color fill_color = Color(0.16f, 0.42f, 0.72f);
	Color grabber_color = Color(0.93f, 0.94f, 0.96f);

	void _set_from_position(const Vector2 &p_local);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void input(const Ref<InputEvent> &p_event) override;
	virtual bool _can_focus() const override { return editable; }

public:
	void set_min(double p_min);
	double get_min() const { return min_value; }
	void set_max(double p_max);
	double get_max() const { return max_value; }
	void set_step(double p_step) { step = std::max(0.0, p_step); }
	double get_step() const { return step; }
	void set_value(double p_value);
	double get_value() const { return value; }
	double get_ratio() const { return max_value > min_value ? (value - min_value) / (max_value - min_value) : 0.0; }
	void set_editable(bool p_editable) { editable = p_editable; }
	bool is_editable() const { return editable; }
	virtual Size2 get_minimum_size() const override { return vertical ? Size2(16, 40) : Size2(40, 16); }

	Slider(bool p_vertical = false);
};

class HSlider : public Slider {
	GDCLASS(HSlider, Slider)

protected:
	static void _bind_methods() {}

public:
	HSlider() :
			Slider(false) {}
};

class VSlider : public Slider {
	GDCLASS(VSlider, Slider)

protected:
	static void _bind_methods() {}

public:
	VSlider() :
			Slider(true) {}
};

// Liste déroulante : un bouton qui ouvre une liste de choix sous lui.
class OptionButton : public Button {
	GDCLASS(OptionButton, Button)

	Array items;
	int selected = -1;
	bool open = false;
	int highlighted = -1;
	RID popup_item;

	real_t _row_height() const;
	Rect2 _popup_rect() const; // repère local du bouton
	void _open(bool p_open);
	void _update_popup();

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void input(const Ref<InputEvent> &p_event) override;
	virtual void _activate() override;
	virtual void _draw_extra() override;

public:
	void add_item(const String &p_text);
	void remove_item(int p_index);
	void clear();
	int get_item_count() const { return items.size(); }
	String get_item_text(int p_index) const;
	void set_item_text(int p_index, const String &p_text);
	void set_items(const Array &p_items);
	Array get_items() const { return items; }
	void select(int p_index);
	int get_selected() const { return selected; }
	virtual Size2 get_minimum_size() const override;

	OptionButton();
	~OptionButton();
};

// Liste d'éléments à sélectionner, avec défilement.
class ItemList : public Control {
	GDCLASS(ItemList, Control)

	Array items;
	int selected = -1;
	int font_size = 16;
	real_t scroll = 0;
	Color bg_color = Color(0.10f, 0.11f, 0.14f, 0.9f);
	Color selection_color = Color(0.16f, 0.42f, 0.72f);
	Color font_color = Color(0.93f, 0.94f, 0.96f);

	real_t _row_height() const;
	real_t _max_scroll() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void input(const Ref<InputEvent> &p_event) override;

public:
	void add_item(const String &p_text);
	void remove_item(int p_index);
	void clear();
	int get_item_count() const { return items.size(); }
	String get_item_text(int p_index) const;
	void set_item_text(int p_index, const String &p_text);
	void set_items(const Array &p_items);
	Array get_items() const { return items; }
	void select(int p_index);
	void deselect_all();
	int get_selected() const { return selected; }
	void ensure_current_is_visible();
	void set_font_size(int p_size);
	int get_font_size() const { return font_size; }

	ItemList();
};

// Onglets : chaque enfant Control est une page, son nom est le titre de l'onglet.
class TabContainer : public Container {
	GDCLASS(TabContainer, Container)

	int current = 0;
	int font_size = 16;
	Dictionary titles; // indice -> titre, quand il diffère du nom du nœud
	int hovered_tab = -1;

	std::vector<Control *> _get_tabs() const;
	real_t _bar_height() const;
	std::vector<Rect2> _get_tab_rects() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _sort_children() override;
	virtual void input(const Ref<InputEvent> &p_event) override;

public:
	void set_current_tab(int p_tab);
	int get_current_tab() const { return current; }
	int get_tab_count() const { return (int)_get_tabs().size(); }
	void set_tab_title(int p_tab, const String &p_title);
	String get_tab_title(int p_tab) const;
	virtual Size2 get_minimum_size() const override;
};

// Grille : range ses enfants en colonnes, ligne après ligne.
class GridContainer : public Container {
	GDCLASS(GridContainer, Container)

	int columns = 2;
	int h_separation = 4;
	int v_separation = 4;

	void _measure(const std::vector<Control *> &p_children, std::vector<real_t> &r_widths, std::vector<real_t> &r_heights, std::vector<bool> &r_expand_columns, std::vector<bool> &r_expand_rows) const;

protected:
	static void _bind_methods();
	virtual void _sort_children() override;

public:
	void set_columns(int p_columns);
	int get_columns() const { return columns; }
	void set_h_separation(int p_separation);
	int get_h_separation() const { return h_separation; }
	void set_v_separation(int p_separation);
	int get_v_separation() const { return v_separation; }
	virtual Size2 get_minimum_size() const override;
};

// Image étirée en neuf zones : les coins gardent leur taille (cadres, bulles, boutons).
class NinePatchRect : public Control {
	GDCLASS(NinePatchRect, Control)

	Ref<Texture2D> texture;
	int margins[4] = { 8, 8, 8, 8 };
	bool draw_center = true;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const { return texture; }
	void set_patch_margin(int p_side, int p_margin);
	void set_patch_margin_left(int v) { set_patch_margin(SIDE_LEFT, v); }
	int get_patch_margin_left() const { return margins[SIDE_LEFT]; }
	void set_patch_margin_top(int v) { set_patch_margin(SIDE_TOP, v); }
	int get_patch_margin_top() const { return margins[SIDE_TOP]; }
	void set_patch_margin_right(int v) { set_patch_margin(SIDE_RIGHT, v); }
	int get_patch_margin_right() const { return margins[SIDE_RIGHT]; }
	void set_patch_margin_bottom(int v) { set_patch_margin(SIDE_BOTTOM, v); }
	int get_patch_margin_bottom() const { return margins[SIDE_BOTTOM]; }
	void set_draw_center(bool p_draw);
	bool is_draw_center_enabled() const { return draw_center; }
	virtual Size2 get_minimum_size() const override { return Size2((real_t)(margins[SIDE_LEFT] + margins[SIDE_RIGHT]), (real_t)(margins[SIDE_TOP] + margins[SIDE_BOTTOM])); }
};

// Trait de séparation dans une boîte horizontale ou verticale.
class Separator : public Control {
	GDCLASS(Separator, Control)

	bool vertical = false;
	Color color = Color(1, 1, 1, 0.2f);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_color(const Color &p_color);
	Color get_color() const { return color; }
	virtual Size2 get_minimum_size() const override { return vertical ? Size2(4, 0) : Size2(0, 4); }
	Separator(bool p_vertical = false) :
			vertical(p_vertical) {}
};

class HSeparator : public Separator {
	GDCLASS(HSeparator, Separator)

protected:
	static void _bind_methods() {}

public:
	HSeparator() :
			Separator(false) {}
};

class VSeparator : public Separator {
	GDCLASS(VSeparator, Separator)

protected:
	static void _bind_methods() {}

public:
	VSeparator() :
			Separator(true) {}
};
