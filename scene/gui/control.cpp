#include "control.h"

#include "scene/main/scene_tree.h"

Size2 Control::get_parent_area_size() const {
	if (const Control *parent_control = Object::cast_to<Control>(get_parent())) {
		return parent_control->get_size();
	}
	if (is_inside_tree() && get_tree()) {
		return get_tree()->get_viewport_size();
	}
	return Size2();
}

void Control::_compute_offsets() {
	const Size2 area = get_parent_area_size();
	offsets[SIDE_LEFT] = position.x - area.x * anchors[SIDE_LEFT];
	offsets[SIDE_TOP] = position.y - area.y * anchors[SIDE_TOP];
	offsets[SIDE_RIGHT] = position.x + size.x - area.x * anchors[SIDE_RIGHT];
	offsets[SIDE_BOTTOM] = position.y + size.y - area.y * anchors[SIDE_BOTTOM];
}

void Control::_apply_rect(const Vector2 &p_position, const Vector2 &p_size) {
	// Un contrôle n'est jamais plus petit que sa taille minimale (texte, contenu d'un conteneur...).
	const Size2 min = get_combined_minimum_size();
	const Vector2 new_size(std::max(min.x, p_size.x), std::max(min.y, p_size.y));
	if (p_position != position) {
		position = p_position;
		_update_canvas_transform();
	}
	if (new_size == size) {
		return;
	}
	size = new_size;
	notification(NOTIFICATION_RESIZED);
	emit_signal("resized");
	queue_redraw();
	if (!is_container()) {
		// Les enfants ancrés suivent le nouveau rectangle.
		for (Node *child : get_children_vector()) {
			if (Control *c = Object::cast_to<Control>(child)) {
				c->update_rect_from_anchors();
			}
		}
	}
}

void Control::update_rect_from_anchors() {
	if (updating_rect) {
		return;
	}
	updating_rect = true;
	const Size2 area = get_parent_area_size();
	const real_t left = area.x * anchors[SIDE_LEFT] + offsets[SIDE_LEFT];
	const real_t top = area.y * anchors[SIDE_TOP] + offsets[SIDE_TOP];
	const real_t right = area.x * anchors[SIDE_RIGHT] + offsets[SIDE_RIGHT];
	const real_t bottom = area.y * anchors[SIDE_BOTTOM] + offsets[SIDE_BOTTOM];
	Vector2 new_position(left, top);
	const Vector2 new_size(right - left, bottom - top);
	// Si la taille minimale dépasse le rectangle demandé, le contrôle grandit à l'opposé de
	// son ancrage : vers la gauche s'il est ancré à droite, des deux côtés s'il est centré.
	const Size2 min = get_combined_minimum_size();
	auto grow = [](real_t p_anchor_begin, real_t p_anchor_end, real_t p_extra, real_t &r_pos) {
		if (p_extra <= 0 || p_anchor_begin != p_anchor_end) {
			return;
		}
		if (p_anchor_begin >= 1) {
			r_pos -= p_extra;
		} else if (p_anchor_begin == 0.5f) {
			r_pos -= std::floor(p_extra * 0.5f);
		}
	};
	grow(anchors[SIDE_LEFT], anchors[SIDE_RIGHT], min.x - new_size.x, new_position.x);
	grow(anchors[SIDE_TOP], anchors[SIDE_BOTTOM], min.y - new_size.y, new_position.y);
	_apply_rect(new_position, new_size);
	updating_rect = false;
}

void Control::set_position(const Vector2 &p_pos) {
	_apply_rect(p_pos, size);
	_compute_offsets();
}

void Control::set_size(const Vector2 &p_size) {
	_apply_rect(position, p_size);
	_compute_offsets();
}

void Control::set_rect_in_container(const Rect2 &p_rect) {
	_apply_rect(p_rect.position, p_rect.size);
	_compute_offsets();
}

void Control::set_global_position(const Vector2 &p_pos) {
	const CanvasItem *parent_item = Object::cast_to<CanvasItem>(get_parent());
	if (parent_item) {
		set_position(parent_item->get_global_transform().affine_inverse().xform(p_pos));
	} else {
		set_position(p_pos);
	}
}

void Control::set_anchor(Side p_side, real_t p_anchor) {
	anchors[p_side] = p_anchor;
	if (is_inside_tree()) {
		_compute_offsets(); // dans l'arbre, le rectangle affiché est conservé
	} else {
		update_rect_from_anchors();
	}
}

void Control::set_offset(Side p_side, real_t p_offset) {
	offsets[p_side] = p_offset;
	update_rect_from_anchors();
}

namespace {
enum AxisMode { AXIS_BEGIN, AXIS_CENTER, AXIS_END, AXIS_WIDE, AXIS_CUSTOM };

struct PresetAxes {
	AxisMode h, v;
};

const PresetAxes PRESETS[] = {
	{ AXIS_BEGIN, AXIS_BEGIN }, // TOP_LEFT
	{ AXIS_END, AXIS_BEGIN }, // TOP_RIGHT
	{ AXIS_BEGIN, AXIS_END }, // BOTTOM_LEFT
	{ AXIS_END, AXIS_END }, // BOTTOM_RIGHT
	{ AXIS_BEGIN, AXIS_CENTER }, // CENTER_LEFT
	{ AXIS_CENTER, AXIS_BEGIN }, // CENTER_TOP
	{ AXIS_END, AXIS_CENTER }, // CENTER_RIGHT
	{ AXIS_CENTER, AXIS_END }, // CENTER_BOTTOM
	{ AXIS_CENTER, AXIS_CENTER }, // CENTER
	{ AXIS_BEGIN, AXIS_WIDE }, // LEFT_WIDE
	{ AXIS_WIDE, AXIS_BEGIN }, // TOP_WIDE
	{ AXIS_END, AXIS_WIDE }, // RIGHT_WIDE
	{ AXIS_WIDE, AXIS_END }, // BOTTOM_WIDE
	{ AXIS_WIDE, AXIS_WIDE }, // FULL_RECT
};

AxisMode _axis_mode(real_t p_begin, real_t p_end) {
	if (p_begin == 0 && p_end == 0) {
		return AXIS_BEGIN;
	}
	if (p_begin == 1 && p_end == 1) {
		return AXIS_END;
	}
	if (p_begin == 0.5f && p_end == 0.5f) {
		return AXIS_CENTER;
	}
	if (p_begin == 0 && p_end == 1) {
		return AXIS_WIDE;
	}
	return AXIS_CUSTOM;
}

void _apply_axis(AxisMode p_mode, real_t p_size, real_t &r_anchor_begin, real_t &r_anchor_end, real_t &r_offset_begin, real_t &r_offset_end) {
	switch (p_mode) {
		case AXIS_BEGIN:
			r_anchor_begin = r_anchor_end = 0;
			r_offset_begin = 0;
			r_offset_end = p_size;
			break;
		case AXIS_END:
			r_anchor_begin = r_anchor_end = 1;
			r_offset_begin = -p_size;
			r_offset_end = 0;
			break;
		case AXIS_CENTER:
			r_anchor_begin = r_anchor_end = 0.5f;
			r_offset_begin = -std::floor(p_size * 0.5f);
			r_offset_end = r_offset_begin + p_size;
			break;
		case AXIS_WIDE:
			r_anchor_begin = 0;
			r_anchor_end = 1;
			r_offset_begin = r_offset_end = 0;
			break;
		default:
			break;
	}
}
} // namespace

void Control::set_anchors_preset(int p_preset) {
	if (p_preset < 0 || p_preset >= PRESET_CUSTOM) {
		return;
	}
	const Size2 min = get_combined_minimum_size();
	const Size2 keep(std::max(size.x, min.x), std::max(size.y, min.y));
	_apply_axis(PRESETS[p_preset].h, keep.x, anchors[SIDE_LEFT], anchors[SIDE_RIGHT], offsets[SIDE_LEFT], offsets[SIDE_RIGHT]);
	_apply_axis(PRESETS[p_preset].v, keep.y, anchors[SIDE_TOP], anchors[SIDE_BOTTOM], offsets[SIDE_TOP], offsets[SIDE_BOTTOM]);
	update_rect_from_anchors();
}

int Control::get_anchors_preset() const {
	const AxisMode h = _axis_mode(anchors[SIDE_LEFT], anchors[SIDE_RIGHT]);
	const AxisMode v = _axis_mode(anchors[SIDE_TOP], anchors[SIDE_BOTTOM]);
	for (int i = 0; i < PRESET_CUSTOM; i++) {
		if (PRESETS[i].h == h && PRESETS[i].v == v) {
			return i;
		}
	}
	return PRESET_CUSTOM;
}

Size2 Control::get_combined_minimum_size() const {
	const Size2 min = get_minimum_size();
	return Size2(std::max(min.x, custom_minimum_size.x), std::max(min.y, custom_minimum_size.y));
}

void Control::update_minimum_size() {
	Control *parent_control = Object::cast_to<Control>(get_parent());
	if (parent_control && parent_control->is_container()) {
		parent_control->notification(NOTIFICATION_SORT_CHILDREN);
	} else {
		// Hors conteneur : le rectangle est recalculé (il grandit ou revient à ses marges).
		update_rect_from_anchors();
	}
}

void Control::set_custom_minimum_size(const Vector2 &p_size) {
	custom_minimum_size = Vector2(std::max(0.0f, p_size.x), std::max(0.0f, p_size.y));
	update_minimum_size();
}

void Control::set_h_size_flags(int p_flags) {
	size_flags_h = p_flags;
	update_minimum_size();
}

void Control::set_v_size_flags(int p_flags) {
	size_flags_v = p_flags;
	update_minimum_size();
}

void Control::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			Control *parent_control = Object::cast_to<Control>(get_parent());
			if (!parent_control || !parent_control->is_container()) {
				update_rect_from_anchors();
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			update_minimum_size();
		} break;
	}
}

Vector2 Control::_event_to_local(const Vector2 &p_screen_pos) const {
	Vector2 canvas_pos = get_canvas_transform().affine_inverse().xform(p_screen_pos);
	return get_global_transform().affine_inverse().xform(canvas_pos);
}

void Control::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_position", "position"), &Control::set_position);
	ClassDB::bind_method(D_METHOD("get_position"), &Control::get_position);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &Control::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &Control::get_size);
	ClassDB::bind_method(D_METHOD("set_global_position", "position"), &Control::set_global_position);
	ClassDB::bind_method(D_METHOD("get_global_position"), &Control::get_global_position);
	ClassDB::bind_method(D_METHOD("get_rect"), &Control::get_rect);
	ClassDB::bind_method(D_METHOD("get_global_rect"), &Control::get_global_rect);
	ClassDB::bind_method(D_METHOD("get_minimum_size"), &Control::get_minimum_size);
	ClassDB::bind_method(D_METHOD("get_combined_minimum_size"), &Control::get_combined_minimum_size);
	ClassDB::bind_method(D_METHOD("set_anchors_preset", "preset"), &Control::set_anchors_preset);
	ClassDB::bind_method(D_METHOD("get_anchors_preset"), &Control::get_anchors_preset);
	ClassDB::bind_method(D_METHOD("set_anchor_left", "anchor"), &Control::set_anchor_left);
	ClassDB::bind_method(D_METHOD("get_anchor_left"), &Control::get_anchor_left);
	ClassDB::bind_method(D_METHOD("set_anchor_top", "anchor"), &Control::set_anchor_top);
	ClassDB::bind_method(D_METHOD("get_anchor_top"), &Control::get_anchor_top);
	ClassDB::bind_method(D_METHOD("set_anchor_right", "anchor"), &Control::set_anchor_right);
	ClassDB::bind_method(D_METHOD("get_anchor_right"), &Control::get_anchor_right);
	ClassDB::bind_method(D_METHOD("set_anchor_bottom", "anchor"), &Control::set_anchor_bottom);
	ClassDB::bind_method(D_METHOD("get_anchor_bottom"), &Control::get_anchor_bottom);
	ClassDB::bind_method(D_METHOD("set_offset_left", "offset"), &Control::set_offset_left);
	ClassDB::bind_method(D_METHOD("get_offset_left"), &Control::get_offset_left);
	ClassDB::bind_method(D_METHOD("set_offset_top", "offset"), &Control::set_offset_top);
	ClassDB::bind_method(D_METHOD("get_offset_top"), &Control::get_offset_top);
	ClassDB::bind_method(D_METHOD("set_offset_right", "offset"), &Control::set_offset_right);
	ClassDB::bind_method(D_METHOD("get_offset_right"), &Control::get_offset_right);
	ClassDB::bind_method(D_METHOD("set_offset_bottom", "offset"), &Control::set_offset_bottom);
	ClassDB::bind_method(D_METHOD("get_offset_bottom"), &Control::get_offset_bottom);
	ClassDB::bind_method(D_METHOD("set_custom_minimum_size", "size"), &Control::set_custom_minimum_size);
	ClassDB::bind_method(D_METHOD("get_custom_minimum_size"), &Control::get_custom_minimum_size);
	ClassDB::bind_method(D_METHOD("set_h_size_flags", "flags"), &Control::set_h_size_flags);
	ClassDB::bind_method(D_METHOD("get_h_size_flags"), &Control::get_h_size_flags);
	ClassDB::bind_method(D_METHOD("set_v_size_flags", "flags"), &Control::set_v_size_flags);
	ClassDB::bind_method(D_METHOD("get_v_size_flags"), &Control::get_v_size_flags);

	// Les ancres et les marges sont enregistrées ; position et taille en découlent.
	ADD_GROUP("Layout");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "anchors_preset", PROPERTY_HINT_ENUM, "Top Left,Top Right,Bottom Left,Bottom Right,Center Left,Center Top,Center Right,Center Bottom,Center,Left Wide,Top Wide,Right Wide,Bottom Wide,Full Rect,Custom", PROPERTY_USAGE_EDITOR), "set_anchors_preset", "get_anchors_preset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anchor_left", PROPERTY_HINT_RANGE, "0,1,0.01", PROPERTY_USAGE_NO_EDITOR), "set_anchor_left", "get_anchor_left");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anchor_top", PROPERTY_HINT_RANGE, "0,1,0.01", PROPERTY_USAGE_NO_EDITOR), "set_anchor_top", "get_anchor_top");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anchor_right", PROPERTY_HINT_RANGE, "0,1,0.01", PROPERTY_USAGE_NO_EDITOR), "set_anchor_right", "get_anchor_right");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anchor_bottom", PROPERTY_HINT_RANGE, "0,1,0.01", PROPERTY_USAGE_NO_EDITOR), "set_anchor_bottom", "get_anchor_bottom");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_left", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_offset_left", "get_offset_left");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_top", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_offset_top", "get_offset_top");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_right", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_offset_right", "get_offset_right");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_bottom", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_offset_bottom", "get_offset_bottom");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "position", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR), "set_position", "get_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "global_position", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_global_position", "get_global_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "custom_minimum_size"), "set_custom_minimum_size", "get_custom_minimum_size");
	ADD_GROUP("Container Sizing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "size_flags_horizontal", PROPERTY_HINT_ENUM, "Fill,Expand Fill,Shrink Begin,Shrink Center,Shrink End"), "set_h_size_flags", "get_h_size_flags");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "size_flags_vertical", PROPERTY_HINT_ENUM, "Fill,Expand Fill,Shrink Begin,Shrink Center,Shrink End"), "set_v_size_flags", "get_v_size_flags");

	ADD_SIGNAL(MethodInfo("resized"));

	BIND_ENUM_CONSTANT(PRESET_TOP_LEFT);
	BIND_ENUM_CONSTANT(PRESET_TOP_RIGHT);
	BIND_ENUM_CONSTANT(PRESET_BOTTOM_LEFT);
	BIND_ENUM_CONSTANT(PRESET_BOTTOM_RIGHT);
	BIND_ENUM_CONSTANT(PRESET_CENTER_LEFT);
	BIND_ENUM_CONSTANT(PRESET_CENTER_TOP);
	BIND_ENUM_CONSTANT(PRESET_CENTER_RIGHT);
	BIND_ENUM_CONSTANT(PRESET_CENTER_BOTTOM);
	BIND_ENUM_CONSTANT(PRESET_CENTER);
	BIND_ENUM_CONSTANT(PRESET_LEFT_WIDE);
	BIND_ENUM_CONSTANT(PRESET_TOP_WIDE);
	BIND_ENUM_CONSTANT(PRESET_RIGHT_WIDE);
	BIND_ENUM_CONSTANT(PRESET_BOTTOM_WIDE);
	BIND_ENUM_CONSTANT(PRESET_FULL_RECT);
	BIND_ENUM_CONSTANT(SIZE_FILL);
	BIND_ENUM_CONSTANT(SIZE_EXPAND_FILL);
	BIND_ENUM_CONSTANT(SIZE_SHRINK_BEGIN);
	BIND_ENUM_CONSTANT(SIZE_SHRINK_CENTER);
	BIND_ENUM_CONSTANT(SIZE_SHRINK_END);
}
