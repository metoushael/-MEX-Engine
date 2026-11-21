// CollisionShape2D : attache une forme (Shape2D) au corps ou à la zone parente.
#pragma once

#include "scene/2d/node_2d.h"
#include "scene/resources/shape_2d.h"

class CollisionShape2D : public Node2D {
	GDCLASS(CollisionShape2D, Node2D)

	Ref<Shape2D> shape;
	bool disabled = false;
	bool one_way_collision = false;
	Color debug_color = Color(0.0f, 0.6f, 0.7f, 0.42f);

	void _shape_changed();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_shape(const Ref<Shape2D> &p_shape);
	Ref<Shape2D> get_shape() const { return shape; }
	void set_disabled(bool p_disabled);
	bool is_disabled() const { return disabled; }
	void set_debug_color(const Color &p_color);
	Color get_debug_color() const { return debug_color; }

	virtual bool _edit_use_rect() const override { return shape.is_valid(); }
	virtual Rect2 _edit_get_rect() const override { return shape.is_valid() ? shape->get_rect() : Node2D::_edit_get_rect(); }
};
