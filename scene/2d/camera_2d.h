// Camera2D : définit la partie du monde affichée à l'écran.
#pragma once

#include "scene/2d/node_2d.h"

class Camera2D : public Node2D {
	GDCLASS(Camera2D, Node2D)

	Vector2 offset;
	Vector2 zoom = Vector2(1, 1);
	bool enabled = true;
	int limit_left = -10000000;
	int limit_top = -10000000;
	int limit_right = 10000000;
	int limit_bottom = 10000000;
	bool smoothing_enabled = false;
	real_t smoothing_speed = 5.0f;

	Vector2 smoothed_position;
	bool first_update = true;

	Vector2 _get_target() const;
	void _update_scroll(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_offset(const Vector2 &p_offset) { offset = p_offset; }
	Vector2 get_offset() const { return offset; }
	void set_zoom(const Vector2 &p_zoom);
	Vector2 get_zoom() const { return zoom; }
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }
	void set_limit_left(int p_limit) { limit_left = p_limit; queue_redraw(); }
	int get_limit_left() const { return limit_left; }
	void set_limit_top(int p_limit) { limit_top = p_limit; queue_redraw(); }
	int get_limit_top() const { return limit_top; }
	void set_limit_right(int p_limit) { limit_right = p_limit; queue_redraw(); }
	int get_limit_right() const { return limit_right; }
	void set_limit_bottom(int p_limit) { limit_bottom = p_limit; queue_redraw(); }
	int get_limit_bottom() const { return limit_bottom; }
	void set_position_smoothing_enabled(bool p_enabled) { smoothing_enabled = p_enabled; }
	bool is_position_smoothing_enabled() const { return smoothing_enabled; }
	void set_position_smoothing_speed(real_t p_speed) { smoothing_speed = std::max(0.0f, p_speed); }
	real_t get_position_smoothing_speed() const { return smoothing_speed; }

	void make_current();
	bool is_current() const;
	void reset_smoothing() { first_update = true; }
	Vector2 get_screen_center_position() const { return smoothed_position; }
	void force_update_scroll();
};
