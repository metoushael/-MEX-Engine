// ScrollContainer : montre une partie d'un contenu plus grand que lui et le fait défiler
// (molette, barre de défilement, ou focus au clavier).
#pragma once

#include "scene/gui/container.h"

class ScrollContainer : public Container {
	GDCLASS(ScrollContainer, Container)

	Vector2 scroll;
	bool horizontal_enabled = false;
	bool vertical_enabled = true;
	int scroll_step = 40;
	RID bar_item; // les barres se dessinent par-dessus le contenu
	bool dragging = false;
	real_t drag_offset = 0;

	Control *_get_content() const;
	Size2 _get_content_size() const;
	Vector2 _get_max_scroll() const;
	Rect2 _get_vertical_grabber() const;
	void _update_bars();

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _sort_children() override;
	virtual void input(const Ref<InputEvent> &p_event) override;
	virtual void _ensure_child_visible(Control *p_child) override;

public:
	static const int BAR_WIDTH = 8;

	void set_scroll_vertical(int p_value);
	int get_scroll_vertical() const { return (int)scroll.y; }
	void set_scroll_horizontal(int p_value);
	int get_scroll_horizontal() const { return (int)scroll.x; }
	void set_horizontal_scroll_enabled(bool p_enabled);
	bool is_horizontal_scroll_enabled() const { return horizontal_enabled; }
	void set_vertical_scroll_enabled(bool p_enabled);
	bool is_vertical_scroll_enabled() const { return vertical_enabled; }
	void set_scroll_step(int p_step) { scroll_step = std::max(1, p_step); }
	int get_scroll_step() const { return scroll_step; }
	void ensure_control_visible(Control *p_control) { _ensure_child_visible(p_control); }

	virtual Size2 get_minimum_size() const override { return Size2(); }

	ScrollContainer();
	~ScrollContainer();
};
