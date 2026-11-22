#include "control.h"

#include "core/input/input.h"
#include "core/string/translation.h"
#include "core/input/input_event.h"
#include "core/input/keyboard.h"
#include "scene/main/scene_tree.h"

ObjectID Control::focus_owner = 0;
ObjectID Control::modal_owner = 0;
Rect2 Control::modal_rect;

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
	if (clip_contents) {
		set_clip_rect(true, Rect2(Vector2(), size));
	}
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
			if (has_focus() && !is_visible_in_tree()) {
				release_focus();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (has_focus()) {
				release_focus();
			}
		} break;
		case NOTIFICATION_TRANSLATION_CHANGED:
		case NOTIFICATION_THEME_CHANGED: {
			queue_redraw();
			update_minimum_size();
		} break;
	}
}

Control::~Control() {
	if (focus_owner == get_instance_id()) {
		focus_owner = 0;
	}
	if (modal_owner == get_instance_id()) {
		modal_owner = 0;
	}
}

String Control::atr(const String &p_text) const {
	return TTR_GAME(p_text);
}

void Control::_set_modal(bool p_enabled, const Rect2 &p_screen_rect) {
	if (p_enabled) {
		modal_owner = get_instance_id();
		modal_rect = p_screen_rect;
	} else if (modal_owner == get_instance_id()) {
		modal_owner = 0;
	}
}

bool Control::_hit(const Vector2 &p_screen_pos) const {
	if (modal_owner && modal_owner != get_instance_id() && modal_rect.has_point(p_screen_pos) && ObjectDB::get_instance(modal_owner)) {
		return false; // caché par une liste déroulante ouverte
	}
	if (!has_point(_event_to_local(p_screen_pos))) {
		return false;
	}
	for (const Node *node = get_parent(); node; node = node->get_parent()) {
		const Control *parent = Object::cast_to<Control>(node);
		if (parent && parent->clip_contents && !parent->has_point(parent->_event_to_local(p_screen_pos))) {
			return false;
		}
	}
	return true;
}

void Control::set_clip_contents(bool p_clip) {
	clip_contents = p_clip;
	set_clip_rect(p_clip, Rect2(Vector2(), size));
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------
Control *Control::get_focus_owner() {
	return focus_owner ? Object::cast_to<Control>(ObjectDB::get_instance(focus_owner)) : nullptr;
}

bool Control::is_focusable() const {
	return focus_mode == FOCUS_ALL && is_inside_tree() && is_visible_in_tree() && _can_focus();
}

void Control::set_focus_mode(int p_mode) {
	focus_mode = p_mode;
	if (focus_mode == FOCUS_NONE && has_focus()) {
		release_focus();
	}
}

void Control::grab_focus() {
	if (has_focus() || focus_mode == FOCUS_NONE || !is_inside_tree() || !_can_focus()) {
		return;
	}
	if (Control *previous = get_focus_owner()) {
		previous->release_focus();
	}
	focus_owner = get_instance_id();
	for (Node *node = get_parent(); node; node = node->get_parent()) {
		if (Control *parent = Object::cast_to<Control>(node)) {
			parent->_ensure_child_visible(this);
		}
	}
	notification(NOTIFICATION_FOCUS_ENTER);
	queue_redraw();
	emit_signal("focus_entered");
}

void Control::release_focus() {
	if (!has_focus()) {
		return;
	}
	focus_owner = 0;
	notification(NOTIFICATION_FOCUS_EXIT);
	queue_redraw();
	emit_signal("focus_exited");
}

static void _collect_focusable(Node *p_node, std::vector<Control *> &r_list) {
	if (Control *control = Object::cast_to<Control>(p_node)) {
		if (control->is_focusable()) {
			r_list.push_back(control);
		}
	}
	for (Node *child : p_node->get_children_vector()) {
		_collect_focusable(child, r_list);
	}
}

Control *Control::find_next_valid_focus() const {
	std::vector<Control *> list;
	_collect_focusable(get_tree()->get_root(), list);
	for (size_t i = 0; i < list.size(); i++) {
		if (list[i] == this) {
			return list[(i + 1) % list.size()];
		}
	}
	return list.empty() ? nullptr : list.front();
}

Control *Control::find_prev_valid_focus() const {
	std::vector<Control *> list;
	_collect_focusable(get_tree()->get_root(), list);
	for (size_t i = 0; i < list.size(); i++) {
		if (list[i] == this) {
			return list[(i + list.size() - 1) % list.size()];
		}
	}
	return list.empty() ? nullptr : list.back();
}

Control *Control::find_focus_neighbor(const Vector2 &p_direction) const {
	std::vector<Control *> list;
	_collect_focusable(get_tree()->get_root(), list);
	const Vector2 from = get_global_position() + get_size() * 0.5f;
	Control *best = nullptr;
	real_t best_score = 0;
	for (Control *candidate : list) {
		if (candidate == this) {
			continue;
		}
		const Vector2 offset = candidate->get_global_position() + candidate->get_size() * 0.5f - from;
		const real_t along = offset.x * p_direction.x + offset.y * p_direction.y;
		const real_t across = std::fabs(offset.x * p_direction.y - offset.y * p_direction.x);
		if (along <= 1 || across > along * 2) {
			continue; // derrière, ou trop de côté
		}
		const real_t score = along + across * 2;
		if (!best || score < best_score) {
			best = candidate;
			best_score = score;
		}
	}
	return best;
}

bool Control::handle_focus_navigation(SceneTree *p_tree, const Ref<InputEvent> &p_event) {
	const InputEventKey *key = Object::cast_to<InputEventKey>(p_event.ptr());
	if (!key || !key->is_pressed()) {
		return false;
	}
	Control *current = get_focus_owner();
	if (current && (!current->is_inside_tree() || current->get_tree() != p_tree)) {
		current = nullptr;
	}
	Control *target = nullptr;
	const int code = key->get_keycode();
	if (code == KEY_TAB) {
		if (current) {
			target = Input::get_singleton()->is_key_pressed(KEY_SHIFT) ? current->find_prev_valid_focus() : current->find_next_valid_focus();
		} else {
			std::vector<Control *> list;
			_collect_focusable(p_tree->get_root(), list);
			target = list.empty() ? nullptr : (Input::get_singleton()->is_key_pressed(KEY_SHIFT) ? list.back() : list.front());
		}
	} else if (current) {
		if (code == KEY_UP) {
			target = current->find_focus_neighbor(Vector2(0, -1));
		} else if (code == KEY_DOWN) {
			target = current->find_focus_neighbor(Vector2(0, 1));
		} else if (code == KEY_LEFT) {
			target = current->find_focus_neighbor(Vector2(-1, 0));
		} else if (code == KEY_RIGHT) {
			target = current->find_focus_neighbor(Vector2(1, 0));
		} else if (code == KEY_ESCAPE) {
			current->release_focus();
			return true;
		}
	}
	if (!target) {
		return false;
	}
	target->grab_focus();
	return true;
}

// ---------------------------------------------------------------------------
// Thème
// ---------------------------------------------------------------------------
static void _propagate_theme_changed(Node *p_node) {
	if (Control *control = Object::cast_to<Control>(p_node)) {
		control->notification(Control::NOTIFICATION_THEME_CHANGED);
	}
	for (Node *child : p_node->get_children_vector()) {
		_propagate_theme_changed(child);
	}
}

void Control::_theme_changed() {
	_propagate_theme_changed(this);
}

void Control::set_theme(const Ref<Theme> &p_theme) {
	if (theme == p_theme) {
		return;
	}
	if (theme.is_valid()) {
		theme->disconnect("changed", Callable(this, "_theme_changed"));
	}
	theme = p_theme;
	if (theme.is_valid()) {
		theme->connect("changed", Callable(this, "_theme_changed"));
	}
	_theme_changed();
}

Variant Control::_theme_lookup(const String &p_name) const {
	for (const Node *node = this; node; node = node->get_parent()) {
		const Control *control = Object::cast_to<Control>(node);
		if (!control || control->theme.is_null()) {
			continue;
		}
		// Les classes dérivées héritent des entrées de leur classe de base.
		for (String type = get_class(); !type.empty() && type != "CanvasItem"; type = ClassDB::get_parent_class(type)) {
			if (control->theme->has_item(p_name, type)) {
				return control->theme->get_item(p_name, type);
			}
		}
	}
	return Variant();
}

Color Control::get_theme_color(const String &p_name, const Color &p_own) const {
	const Variant value = _theme_lookup(p_name);
	return value.get_type() == Variant::COLOR ? value.as_color() : p_own;
}

int Control::get_theme_constant(const String &p_name, int p_own) const {
	const Variant value = _theme_lookup(p_name);
	return value.is_num() ? (int)value.as_int() : p_own;
}

int Control::get_theme_font_size(const String &p_name, int p_own) const {
	const Variant value = _theme_lookup(p_name);
	if (value.is_num()) {
		return (int)value.as_int();
	}
	for (const Node *node = this; node; node = node->get_parent()) {
		const Control *control = Object::cast_to<Control>(node);
		if (control && control->theme.is_valid() && control->theme->get_default_font_size() > 0) {
			return control->theme->get_default_font_size();
		}
	}
	return p_own;
}

Ref<Font> Control::get_theme_font(const String &p_name, const Ref<Font> &p_own) const {
	if (p_own.is_valid()) {
		return p_own; // une police choisie sur le contrôle l'emporte
	}
	const Variant value = _theme_lookup(p_name);
	if (Font *font = Object::cast_to<Font>(value.as_object())) {
		return Ref<Font>(font);
	}
	for (const Node *node = this; node; node = node->get_parent()) {
		const Control *control = Object::cast_to<Control>(node);
		if (control && control->theme.is_valid() && control->theme->get_default_font().is_valid()) {
			return control->theme->get_default_font();
		}
	}
	return Font::get_default();
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

	ClassDB::bind_method(D_METHOD("set_focus_mode", "mode"), &Control::set_focus_mode);
	ClassDB::bind_method(D_METHOD("get_focus_mode"), &Control::get_focus_mode);
	ClassDB::bind_method(D_METHOD("grab_focus"), &Control::grab_focus);
	ClassDB::bind_method(D_METHOD("release_focus"), &Control::release_focus);
	ClassDB::bind_method(D_METHOD("has_focus"), &Control::has_focus);
	ClassDB::bind_method(D_METHOD("set_clip_contents", "clip"), &Control::set_clip_contents);
	ClassDB::bind_method(D_METHOD("is_clipping_contents"), &Control::is_clipping_contents);
	ClassDB::bind_method(D_METHOD("set_theme", "theme"), &Control::set_theme);
	ClassDB::bind_method(D_METHOD("get_theme"), &Control::get_theme);
	ClassDB::bind_method(D_METHOD("get_theme_color", "name", "own"), &Control::get_theme_color);
	ClassDB::bind_method(D_METHOD("set_tooltip_text", "text"), &Control::set_tooltip_text);
	ClassDB::bind_method(D_METHOD("get_tooltip_text"), &Control::get_tooltip_text);
	ClassDB::bind_method(D_METHOD("_theme_changed"), &Control::_theme_changed);
	ADD_GROUP("Behavior");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "focus_mode", PROPERTY_HINT_ENUM, "None,Click,All"), "set_focus_mode", "get_focus_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clip_contents"), "set_clip_contents", "is_clipping_contents");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "theme", PROPERTY_HINT_RESOURCE_TYPE, "Theme"), "set_theme", "get_theme");

	ADD_SIGNAL(MethodInfo("resized"));
	ADD_SIGNAL(MethodInfo("focus_entered"));
	ADD_SIGNAL(MethodInfo("focus_exited"));
	BIND_ENUM_CONSTANT(FOCUS_NONE);
	BIND_ENUM_CONSTANT(FOCUS_CLICK);
	BIND_ENUM_CONSTANT(FOCUS_ALL);

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
