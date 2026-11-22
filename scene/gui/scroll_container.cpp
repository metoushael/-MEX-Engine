#include "scroll_container.h"

#include "core/input/input_event.h"
#include "core/input/keyboard.h"
#include "scene/main/scene_tree.h"

#include <climits>

ScrollContainer::ScrollContainer() {
	RenderingServer *rs = RenderingServer::get_singleton();
	bar_item = rs->canvas_item_create();
	rs->canvas_item_set_parent(bar_item, get_canvas_item());
	rs->canvas_item_set_draw_index(bar_item, INT_MAX); // après tous les enfants
	set_clip_contents(true);
	set_size(Vector2(200, 160));
}

ScrollContainer::~ScrollContainer() {
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(bar_item);
	}
}

Control *ScrollContainer::_get_content() const {
	const std::vector<Control *> children = _get_layout_children();
	return children.empty() ? nullptr : children.front();
}

Size2 ScrollContainer::_get_content_size() const {
	Control *content = _get_content();
	return content ? content->get_combined_minimum_size() : Size2();
}

Vector2 ScrollContainer::_get_max_scroll() const {
	const Size2 content = _get_content_size();
	const Size2 size = get_size();
	return Vector2(horizontal_enabled ? std::max((real_t)0, content.x - size.x) : 0, vertical_enabled ? std::max((real_t)0, content.y - size.y) : 0);
}

void ScrollContainer::_sort_children() {
	const Vector2 max_scroll = _get_max_scroll();
	scroll.x = Math::clamp<real_t>(scroll.x, 0, max_scroll.x);
	scroll.y = Math::clamp<real_t>(scroll.y, 0, max_scroll.y);
	if (Control *content = _get_content()) {
		const Size2 min = content->get_combined_minimum_size();
		const Size2 size = get_size();
		// La place de la barre n'est retirée que si elle est affichée.
		const real_t width = size.x - (max_scroll.y > 0 ? BAR_WIDTH + 2 : 0);
		const Size2 content_size(horizontal_enabled ? std::max(min.x, width) : width, vertical_enabled ? std::max(min.y, size.y) : size.y);
		content->set_rect_in_container(Rect2(-scroll.round(), content_size));
	}
	_update_bars();
}

Rect2 ScrollContainer::_get_vertical_grabber() const {
	const Size2 size = get_size();
	const real_t max_scroll = _get_max_scroll().y;
	if (max_scroll <= 0) {
		return Rect2();
	}
	const real_t content = size.y + max_scroll;
	const real_t length = std::max((real_t)24, size.y * size.y / content);
	const real_t top = (size.y - length) * (scroll.y / max_scroll);
	return Rect2(size.x - BAR_WIDTH, top, BAR_WIDTH, length);
}

void ScrollContainer::_update_bars() {
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->canvas_item_clear(bar_item);
	const Rect2 grabber = _get_vertical_grabber();
	if (grabber.size.y <= 0) {
		return;
	}
	const Size2 size = get_size();
	rs->canvas_item_add_rect(bar_item, Rect2(size.x - BAR_WIDTH, 0, BAR_WIDTH, size.y), get_theme_color("scroll_bg_color", Color(0, 0, 0, 0.25f)));
	rs->canvas_item_add_rect(bar_item, grabber, get_theme_color("scroll_grabber_color", dragging ? Color(1, 1, 1, 0.75f) : Color(1, 1, 1, 0.45f)));
}

void ScrollContainer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_input(!get_tree()->is_edit_mode());
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			_update_bars();
		} break;
	}
}

void ScrollContainer::input(const Ref<InputEvent> &p_event) {
	if (!is_visible_in_tree()) {
		return;
	}
	if (InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(p_event.ptr())) {
		const int button = mb->get_button_index();
		if (mb->is_pressed() && (button == MOUSE_BUTTON_WHEEL_UP || button == MOUSE_BUTTON_WHEEL_DOWN)) {
			if (_hit(mb->get_position()) && _get_max_scroll().y > 0) {
				const int before = get_scroll_vertical();
				set_scroll_vertical(before + (button == MOUSE_BUTTON_WHEEL_UP ? -scroll_step : scroll_step));
				if (get_scroll_vertical() != before) {
					get_tree()->set_input_as_handled(); // en butée, un conteneur parent prend le relais
				}
			}
			return;
		}
		if (button != MOUSE_BUTTON_LEFT) {
			return;
		}
		if (!mb->is_pressed()) {
			if (dragging) {
				dragging = false;
				_update_bars();
			}
			return;
		}
		const Vector2 local = _event_to_local(mb->get_position());
		const Rect2 grabber = _get_vertical_grabber();
		if (grabber.size.y > 0 && _hit(mb->get_position()) && local.x >= get_size().x - BAR_WIDTH) {
			if (grabber.has_point(local)) {
				drag_offset = local.y - grabber.position.y;
			} else {
				drag_offset = grabber.size.y * 0.5f; // clic sur la piste : le curseur s'y place
			}
			dragging = true;
			const real_t track = get_size().y - grabber.size.y;
			set_scroll_vertical((int)(track > 0 ? (local.y - drag_offset) / track * _get_max_scroll().y : 0));
			get_tree()->set_input_as_handled();
		}
		return;
	}
	if (InputEventMouseMotion *motion = Object::cast_to<InputEventMouseMotion>(p_event.ptr())) {
		if (dragging) {
			const Rect2 grabber = _get_vertical_grabber();
			const real_t track = get_size().y - grabber.size.y;
			const Vector2 local = _event_to_local(motion->get_position());
			set_scroll_vertical((int)(track > 0 ? (local.y - drag_offset) / track * _get_max_scroll().y : 0));
		}
	}
}

void ScrollContainer::_ensure_child_visible(Control *p_child) {
	if (!p_child || !is_inside_tree()) {
		return;
	}
	// Rectangle de l'enfant dans le repère du conteneur.
	const Vector2 top_left = p_child->get_global_position() - get_global_position();
	const Vector2 bottom_right = top_left + p_child->get_size();
	const Size2 size = get_size();
	if (vertical_enabled) {
		if (top_left.y < 0) {
			set_scroll_vertical((int)std::floor(scroll.y + top_left.y));
		} else if (bottom_right.y > size.y) {
			set_scroll_vertical((int)std::ceil(scroll.y + bottom_right.y - size.y));
		}
	}
	if (horizontal_enabled) {
		if (top_left.x < 0) {
			set_scroll_horizontal((int)std::floor(scroll.x + top_left.x));
		} else if (bottom_right.x > size.x) {
			set_scroll_horizontal((int)std::ceil(scroll.x + bottom_right.x - size.x));
		}
	}
}

void ScrollContainer::set_scroll_vertical(int p_value) {
	const real_t value = Math::clamp<real_t>((real_t)p_value, 0, _get_max_scroll().y);
	if (value == scroll.y) {
		return;
	}
	scroll.y = value;
	queue_sort();
	emit_signal("scrolled");
}

void ScrollContainer::set_scroll_horizontal(int p_value) {
	const real_t value = Math::clamp<real_t>((real_t)p_value, 0, _get_max_scroll().x);
	if (value == scroll.x) {
		return;
	}
	scroll.x = value;
	queue_sort();
	emit_signal("scrolled");
}

void ScrollContainer::set_horizontal_scroll_enabled(bool p_enabled) {
	horizontal_enabled = p_enabled;
	queue_sort();
}

void ScrollContainer::set_vertical_scroll_enabled(bool p_enabled) {
	vertical_enabled = p_enabled;
	queue_sort();
}

void ScrollContainer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_scroll_vertical", "value"), &ScrollContainer::set_scroll_vertical);
	ClassDB::bind_method(D_METHOD("get_scroll_vertical"), &ScrollContainer::get_scroll_vertical);
	ClassDB::bind_method(D_METHOD("set_scroll_horizontal", "value"), &ScrollContainer::set_scroll_horizontal);
	ClassDB::bind_method(D_METHOD("get_scroll_horizontal"), &ScrollContainer::get_scroll_horizontal);
	ClassDB::bind_method(D_METHOD("set_horizontal_scroll_enabled", "enabled"), &ScrollContainer::set_horizontal_scroll_enabled);
	ClassDB::bind_method(D_METHOD("is_horizontal_scroll_enabled"), &ScrollContainer::is_horizontal_scroll_enabled);
	ClassDB::bind_method(D_METHOD("set_vertical_scroll_enabled", "enabled"), &ScrollContainer::set_vertical_scroll_enabled);
	ClassDB::bind_method(D_METHOD("is_vertical_scroll_enabled"), &ScrollContainer::is_vertical_scroll_enabled);
	ClassDB::bind_method(D_METHOD("set_scroll_step", "step"), &ScrollContainer::set_scroll_step);
	ClassDB::bind_method(D_METHOD("get_scroll_step"), &ScrollContainer::get_scroll_step);
	ClassDB::bind_method(D_METHOD("ensure_control_visible", "control"), &ScrollContainer::ensure_control_visible);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "horizontal_scroll_enabled"), "set_horizontal_scroll_enabled", "is_horizontal_scroll_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "vertical_scroll_enabled"), "set_vertical_scroll_enabled", "is_vertical_scroll_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "scroll_step", PROPERTY_HINT_RANGE, "1,400,1"), "set_scroll_step", "get_scroll_step");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "scroll_horizontal", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_scroll_horizontal", "get_scroll_horizontal");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "scroll_vertical", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_scroll_vertical", "get_scroll_vertical");
	ADD_SIGNAL(MethodInfo("scrolled"));
}
