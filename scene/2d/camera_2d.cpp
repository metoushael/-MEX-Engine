#include "camera_2d.h"

#include "scene/main/scene_tree.h"

void Camera2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			first_update = true;
			if (get_tree()->is_edit_mode()) {
				return;
			}
			if (enabled && !ObjectDB::get_instance(get_tree()->get_current_camera())) {
				make_current();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (is_current()) {
				get_tree()->set_current_camera(0);
				get_tree()->set_canvas_transform(Transform2D());
			}
		} break;
		case SceneTree::NOTIFICATION_UPDATE_CAMERA: {
			_update_scroll(get_process_delta_time());
		} break;
		case NOTIFICATION_DRAW: {
			if (!get_tree()->is_edit_mode()) {
				return;
			}
			// Cadre de la vue dans l'éditeur (indépendant de l'échelle du nœud).
			Size2 size = get_viewport_size() / zoom;
			Transform2D inv = get_global_transform().affine_inverse();
			Vector2 center = get_global_position() + offset;
			std::vector<Vector2> pts = {
				inv.xform(center + Vector2(-size.x, -size.y) * 0.5f),
				inv.xform(center + Vector2(size.x, -size.y) * 0.5f),
				inv.xform(center + Vector2(size.x, size.y) * 0.5f),
				inv.xform(center + Vector2(-size.x, size.y) * 0.5f),
			};
			draw_polyline(pts, Color(0.85f, 0.45f, 0.95f, 0.9f), 2.0f, true);
		} break;
	}
}

void Camera2D::set_zoom(const Vector2 &p_zoom) {
	ERR_FAIL_COND_MSG(p_zoom.x <= 0 || p_zoom.y <= 0, "Camera2D : le zoom doit être strictement positif.");
	zoom = p_zoom;
	queue_redraw();
}

void Camera2D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (!is_inside_tree() || get_tree()->is_edit_mode()) {
		return;
	}
	if (!enabled && is_current()) {
		get_tree()->set_current_camera(0);
		get_tree()->set_canvas_transform(Transform2D());
	} else if (enabled && !ObjectDB::get_instance(get_tree()->get_current_camera())) {
		make_current();
	}
}

void Camera2D::make_current() {
	ERR_FAIL_COND_MSG(!is_inside_tree(), "Camera2D : le nœud doit être dans l'arbre.");
	get_tree()->set_current_camera(get_instance_id());
	first_update = true;
	_update_scroll(0);
}

bool Camera2D::is_current() const {
	return is_inside_tree() && get_tree()->get_current_camera() == get_instance_id();
}

void Camera2D::force_update_scroll() {
	if (is_current()) {
		_update_scroll(0);
	}
}

Vector2 Camera2D::_get_target() const {
	return get_global_position() + offset;
}

void Camera2D::_update_scroll(double p_delta) {
	if (!is_current()) {
		return;
	}
	const Size2 screen = get_viewport_size();
	const Size2 half = screen / zoom * 0.5f;
	Vector2 target = _get_target();

	// Limites : la vue ne sort pas du rectangle autorisé.
	const real_t min_x = (real_t)limit_left + half.x, max_x = (real_t)limit_right - half.x;
	const real_t min_y = (real_t)limit_top + half.y, max_y = (real_t)limit_bottom - half.y;
	target.x = min_x > max_x ? ((real_t)limit_left + (real_t)limit_right) * 0.5f : Math::clamp(target.x, min_x, max_x);
	target.y = min_y > max_y ? ((real_t)limit_top + (real_t)limit_bottom) * 0.5f : Math::clamp(target.y, min_y, max_y);

	if (smoothing_enabled && !first_update) {
		real_t t = Math::clamp((real_t)(smoothing_speed * p_delta), 0.0f, 1.0f);
		smoothed_position = smoothed_position.lerp(target, t);
	} else {
		smoothed_position = target;
	}
	first_update = false;

	Transform2D xform;
	xform.x = Vector2(zoom.x, 0);
	xform.y = Vector2(0, zoom.y);
	xform.origin = screen * 0.5f - smoothed_position * zoom;
	// Arrondi au pixel : évite le scintillement des sprites.
	xform.origin = xform.origin.round();
	get_tree()->set_canvas_transform(xform);
}

void Camera2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_offset", "offset"), &Camera2D::set_offset);
	ClassDB::bind_method(D_METHOD("get_offset"), &Camera2D::get_offset);
	ClassDB::bind_method(D_METHOD("set_zoom", "zoom"), &Camera2D::set_zoom);
	ClassDB::bind_method(D_METHOD("get_zoom"), &Camera2D::get_zoom);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &Camera2D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &Camera2D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_limit_left", "limit"), &Camera2D::set_limit_left);
	ClassDB::bind_method(D_METHOD("get_limit_left"), &Camera2D::get_limit_left);
	ClassDB::bind_method(D_METHOD("set_limit_top", "limit"), &Camera2D::set_limit_top);
	ClassDB::bind_method(D_METHOD("get_limit_top"), &Camera2D::get_limit_top);
	ClassDB::bind_method(D_METHOD("set_limit_right", "limit"), &Camera2D::set_limit_right);
	ClassDB::bind_method(D_METHOD("get_limit_right"), &Camera2D::get_limit_right);
	ClassDB::bind_method(D_METHOD("set_limit_bottom", "limit"), &Camera2D::set_limit_bottom);
	ClassDB::bind_method(D_METHOD("get_limit_bottom"), &Camera2D::get_limit_bottom);
	ClassDB::bind_method(D_METHOD("set_position_smoothing_enabled", "enabled"), &Camera2D::set_position_smoothing_enabled);
	ClassDB::bind_method(D_METHOD("is_position_smoothing_enabled"), &Camera2D::is_position_smoothing_enabled);
	ClassDB::bind_method(D_METHOD("set_position_smoothing_speed", "speed"), &Camera2D::set_position_smoothing_speed);
	ClassDB::bind_method(D_METHOD("get_position_smoothing_speed"), &Camera2D::get_position_smoothing_speed);
	ClassDB::bind_method(D_METHOD("make_current"), &Camera2D::make_current);
	ClassDB::bind_method(D_METHOD("is_current"), &Camera2D::is_current);
	ClassDB::bind_method(D_METHOD("reset_smoothing"), &Camera2D::reset_smoothing);
	ClassDB::bind_method(D_METHOD("get_screen_center_position"), &Camera2D::get_screen_center_position);
	ClassDB::bind_method(D_METHOD("force_update_scroll"), &Camera2D::force_update_scroll);

	ADD_GROUP("Camera");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset"), "set_offset", "get_offset");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "zoom"), "set_zoom", "get_zoom");
	ADD_GROUP("Limit");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limit_left"), "set_limit_left", "get_limit_left");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limit_top"), "set_limit_top", "get_limit_top");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limit_right"), "set_limit_right", "get_limit_right");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limit_bottom"), "set_limit_bottom", "get_limit_bottom");
	ADD_GROUP("Position Smoothing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "position_smoothing_enabled"), "set_position_smoothing_enabled", "is_position_smoothing_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "position_smoothing_speed", PROPERTY_HINT_RANGE, "0,100,0.1"), "set_position_smoothing_speed", "get_position_smoothing_speed");
}
