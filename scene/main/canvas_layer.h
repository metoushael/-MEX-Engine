// CanvasLayer : calque de dessin indépendant de la caméra (interface, HUD, arrière-plans).
#pragma once

#include "scene/main/node.h"
#include "servers/rendering_server.h"

class CanvasLayer : public Node {
	GDCLASS(CanvasLayer, Node)

	RID canvas;
	int layer = 1;
	Vector2 offset;
	Vector2 scale = Vector2(1, 1);
	bool visible = true;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	RID get_canvas() const { return canvas; }
	void set_layer(int p_layer) { layer = p_layer; }
	int get_layer() const { return layer; }
	void set_offset(const Vector2 &p_offset) { offset = p_offset; }
	Vector2 get_offset() const { return offset; }
	void set_scale(const Vector2 &p_scale) { scale = p_scale; }
	Vector2 get_scale() const { return scale; }
	void set_visible(bool p_visible) { visible = p_visible; }
	bool is_visible() const { return visible; }
	Transform2D get_layer_transform() const { return Transform2D(0, scale, offset); }

	CanvasLayer();
	~CanvasLayer();
};
