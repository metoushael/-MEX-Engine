// Sprite2D : affiche une texture (avec découpe en grille d'images ou en région).
#pragma once

#include "scene/2d/node_2d.h"

class Sprite2D : public Node2D {
	GDCLASS(Sprite2D, Node2D)

	Ref<Texture2D> texture;
	bool centered = true;
	Vector2 offset;
	bool flip_h = false;
	bool flip_v = false;
	int hframes = 1;
	int vframes = 1;
	int frame = 0;
	bool region_enabled = false;
	Rect2 region_rect;

	Rect2 _get_source_rect() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const { return texture; }
	void set_centered(bool p_centered);
	bool is_centered() const { return centered; }
	void set_offset(const Vector2 &p_offset);
	Vector2 get_offset() const { return offset; }
	void set_flip_h(bool p_flip);
	bool is_flipped_h() const { return flip_h; }
	void set_flip_v(bool p_flip);
	bool is_flipped_v() const { return flip_v; }
	void set_hframes(int p_frames);
	int get_hframes() const { return hframes; }
	void set_vframes(int p_frames);
	int get_vframes() const { return vframes; }
	void set_frame(int p_frame);
	int get_frame() const { return frame; }
	void set_region_enabled(bool p_enabled);
	bool is_region_enabled() const { return region_enabled; }
	void set_region_rect(const Rect2 &p_rect);
	Rect2 get_region_rect() const { return region_rect; }

	Rect2 get_rect() const;
	virtual bool _edit_use_rect() const override { return texture.is_valid(); }
	virtual Rect2 _edit_get_rect() const override { return texture.is_valid() ? get_rect() : Node2D::_edit_get_rect(); }
};
