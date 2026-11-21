// Conteneurs : placent automatiquement leurs enfants Control.
#pragma once

#include "scene/gui/control.h"

class Container : public Control {
	GDCLASS(Container, Control)

	bool sorting = false;
	bool sort_again = false;
	Size2 last_minimum_size;

protected:
	void _notification(int p_what);
	static void _bind_methods() {}
	virtual void _sort_children() {}
	// Enfants Control visibles, dans l'ordre de l'arbre.
	std::vector<Control *> _get_layout_children() const;
	// Place un enfant dans la zone donnée selon ses drapeaux de taille.
	void _fit_child_in_rect(Control *p_child, const Rect2 &p_rect) const;

public:
	virtual bool is_container() const override { return true; }
	void queue_sort();
};

class BoxContainer : public Container {
	GDCLASS(BoxContainer, Container)

public:
	enum AlignmentMode {
		ALIGNMENT_BEGIN,
		ALIGNMENT_CENTER,
		ALIGNMENT_END,
	};

private:
	bool vertical = false;
	int separation = 4;
	int alignment = ALIGNMENT_BEGIN;

protected:
	static void _bind_methods();
	virtual void _sort_children() override;

public:
	void set_separation(int p_separation);
	int get_separation() const { return separation; }
	void set_alignment(int p_alignment);
	int get_alignment() const { return alignment; }
	virtual Size2 get_minimum_size() const override;

	BoxContainer(bool p_vertical = false) :
			vertical(p_vertical) {}
};

class VBoxContainer : public BoxContainer {
	GDCLASS(VBoxContainer, BoxContainer)

protected:
	static void _bind_methods() {}

public:
	VBoxContainer() :
			BoxContainer(true) {}
};

class HBoxContainer : public BoxContainer {
	GDCLASS(HBoxContainer, BoxContainer)

protected:
	static void _bind_methods() {}

public:
	HBoxContainer() :
			BoxContainer(false) {}
};

class MarginContainer : public Container {
	GDCLASS(MarginContainer, Container)

	int margins[4] = { 8, 8, 8, 8 };

protected:
	static void _bind_methods();
	virtual void _sort_children() override;

public:
	void set_margin(int p_side, int p_margin);
	void set_margin_left(int v) { set_margin(SIDE_LEFT, v); }
	int get_margin_left() const { return margins[SIDE_LEFT]; }
	void set_margin_top(int v) { set_margin(SIDE_TOP, v); }
	int get_margin_top() const { return margins[SIDE_TOP]; }
	void set_margin_right(int v) { set_margin(SIDE_RIGHT, v); }
	int get_margin_right() const { return margins[SIDE_RIGHT]; }
	void set_margin_bottom(int v) { set_margin(SIDE_BOTTOM, v); }
	int get_margin_bottom() const { return margins[SIDE_BOTTOM]; }
	virtual Size2 get_minimum_size() const override;
};

class CenterContainer : public Container {
	GDCLASS(CenterContainer, Container)

protected:
	static void _bind_methods() {}
	virtual void _sort_children() override;

public:
	virtual Size2 get_minimum_size() const override;
};
